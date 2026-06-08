#ifndef TYLIB_LOG_LOG_H_
#define TYLIB_LOG_LOG_H_

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/ipc.h>
#include <sys/mman.h>
#include <sys/sem.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace tylib {

#if !__GLIBC_PREREQ(2, 3)
#define __builtin_expect(x, expected_value) (x)
#endif

#ifndef likely
#define likely(x) __builtin_expect(!!(x), 1)
#endif

#ifndef unlikely
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

#if _WIN32
#define STRIP_FILENAME(x) strrchr(x, '\\') ? strrchr(x, '\\') + 1 : x
#else
#define STRIP_FILENAME(x) strrchr(x, '/') ? strrchr(x, '/') + 1 : x
#endif

enum MLOG_FMT {
  MLOG_F_NONE = 0,
  MLOG_F_PNAME = 1,
  MLOG_F_LEVEL = 2,
  MLOG_F_TIME = 4,
  MLOG_F_PID = 8,
  MLOG_F_TID = 16,
  MLOG_F_FILELINE = 32,
  MLOG_F_FUNC = 64,
  MLOG_F_ALL = 0xFFFFFFFF
};

enum MLOG_LEVEL {
  MLOG_LV_ERROR = 1,
  MLOG_LV_NORMAL = 2,
  MLOG_LV_DEBUG = 3,
  MLOG_LV_TRACE = 4,
};

namespace mlog {

static inline pid_t gettid(void) { return syscall(__NR_gettid); }

static inline const char* getpname(void) {
  static char pname[1024] = {0};
  if (pname[0] == 0) {
    int fd, n;
    char t[1024];
    snprintf(t, sizeof(t), "/proc/%d/cmdline", getpid());
    fd = open(t, O_RDONLY);
    if (fd < 0) goto _error_exit;
    n = read(fd, t, sizeof(t));
    close(fd);
    if (n <= 0) goto _error_exit;
    t[sizeof(t) / sizeof(t[0]) - 1] = 0;
    n = strlen(t);
    if (n <= 0) goto _error_exit;
    while (n >= 0 && t[n] != '/') n--;
    n++;
    strncpy(pname, t + n, sizeof(pname) - 1);
  _error_exit:
    if (pname[0] == 0) strncpy(pname, "unknown", sizeof(pname) - 1);
  }

  return pname;
}

static inline int sem_lock(int semid) {
  struct sembuf op_down = {0, -1, SEM_UNDO};
  return semop(semid, &op_down, 1);
}

static inline int sem_unlock(int semid) {
  struct sembuf op_up = {0, 1, SEM_UNDO};
  return semop(semid, &op_up, 1);
}

inline int sem_init_count(key_t semkey, int count) {
  int semid;

  if ((semid = semget(semkey, 0, 0)) == -1) {
    if ((semid = semget(semkey, 1,
                        IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR | S_IRGRP |
                            S_IWGRP | S_IROTH | S_IWOTH)) != -1) {
      struct sembuf sbuf;
      sbuf.sem_num = 0;
      sbuf.sem_op = count;
      sbuf.sem_flg = 0;
      if (semop(semid, &sbuf, 1) == -1) {
        return -1;
      }
    } else if (errno == EEXIST) {
      if ((semid = semget(semkey, 0, 0)) == -1) {
        return -1;
      }
    } else {
      return -1;
    }
  }

  return semid;
}

class CMLogger {
 public:
  CMLogger();
  virtual ~CMLogger();

 public:
  int Init(int _mylevel, unsigned _format, const char* _dir,
           const char* _prefix, unsigned long _size);

  int Log(int level, const char* file, int line, const char* func,
          const char* fmt, ...) __attribute__((format(printf, 6, 7)));

  int Level() { return mylevel; }

 private:
  struct mmap_struct {
    long ts;              // 4 bytes in 32-bit mode, 8 bytes in 64-bit mode
    unsigned long bytes;  // 4 bytes in 32-bit mode, 8 bytes in 64-bit mode
  };  // 8 bytes in 32-bit mode, 16 bytes in 64-bit mode

  std::string MakeName(long ts);

  void Clean();

  int Log(const char* buf, int len);

 private:
  int mylevel;
  unsigned format;
  std::string dir;
  std::string prefix;

  // 4 bytes in 32-bit mode, 8 bytes in 64-bit mode
  unsigned long size;

 private:
  int lkfd;
  int semid;
  volatile mmap_struct* mm;

 private:
  long myts;  // 4 bytes in 32-bit mode, 8 bytes in 64-bit mode
  int fd;
  int init;
};

inline CMLogger::CMLogger()
    : mylevel(0),
      format(0),
      size(0),
      lkfd(-1),
      semid(-1),
      mm(0),
      myts(0),
      fd(-1),
      init(0) {}

inline CMLogger::~CMLogger() { Clean(); }

inline void CMLogger::Clean() {
  if (fd >= 0) {
    close(fd);
  }
  if (lkfd >= 0) {
    close(lkfd);
  }
  if (mm && mm != MAP_FAILED) {
    munmap(const_cast<mmap_struct*>(mm), sizeof(mmap_struct));
  }
}

inline std::string CMLogger::MakeName(long ts) {
  struct tm t;
  time_t tt = ts;
  localtime_r(&tt, &t);
  char tmp[1024];
  snprintf(tmp, sizeof(tmp), "%s/%s_%4d%02d%02d_%02d%02d%02d.log", dir.c_str(),
           prefix.c_str(), t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour,
           t.tm_min, t.tm_sec);

  const char* softlinkTarget = tmp + dir.size() + 1;

  char softlink[1024];
  snprintf(softlink, sizeof(softlink), "%s/%s.log", dir.c_str(),
           prefix.c_str());

  // symlink cannot force create link
  remove(softlink);
  symlink(softlinkTarget, softlink);

  return tmp;
}

inline int CMLogger::Init(int _mylevel, unsigned _format, const char* _dir,
                          const char* _prefix, unsigned long _size) {
  if (init) return 0;

  int ret = 0;

  dir.clear();
  prefix.clear();

  mylevel = _mylevel;
  format = _format;
  if (_dir) dir = _dir;
  if (_prefix) prefix = _prefix;
  size = _size;

  if (dir.empty()) dir = ".";
  if (prefix.empty()) prefix = getpname();

  if (size <= 0) size = 1024 * 1024 * 1024;

  struct stat sb;
  std::string lkfile = dir + "/.mlog." + prefix;

  if ((lkfd = open(lkfile.c_str(), O_RDWR | O_CREAT, 0666)) < 0) {
    Clean();
    return -1;
  }

  key_t semkey = ftok(lkfile.c_str(), 81);
  if ((semid = sem_init_count(semkey, 1)) == -1) {
    Clean();
    return -2;
  }

  sem_lock(semid);
  if (fstat(lkfd, &sb) != 0)
    ret = -3;
  else if (sb.st_size != sizeof(mmap_struct)) {
    if (sb.st_size && ftruncate(lkfd, 0) != 0)
      ret = -4;
    else {
      mmap_struct md = {time(nullptr), 0};
      if (write(lkfd, &md, sizeof(mmap_struct)) != sizeof(mmap_struct))
        ret = -5;
    }
  }
  sem_unlock(semid);

  if (ret) {
    Clean();
    return ret;
  }

  mm = static_cast<mmap_struct*>(mmap(
      0, sizeof(mmap_struct), PROT_READ | PROT_WRITE, MAP_SHARED, lkfd, 0));
  if (mm == MAP_FAILED) {
    Clean();
    return -6;
  }

  myts = 0;
  fd = -1;
  init = 1;
  return 0;
}

inline int CMLogger::Log(int level, const char* file, int line,
                         const char* func, const char* fmt, ...) {
  if (level > mylevel) return 0;

  char buf[8 * 1024];
  int n = 0;

  if (format & MLOG_F_PNAME) {
    n += snprintf(buf + n, sizeof(buf) - n, "%s ", getpname());
  }

  if (format & MLOG_F_LEVEL) {
    n += snprintf(buf + n, sizeof(buf) - n, "%d ", level);
  }

  if (format & MLOG_F_TIME) {
    struct timespec tspec;
    clock_gettime(CLOCK_REALTIME, &tspec);

    struct tm t;
    localtime_r(&tspec.tv_sec, &t);
    int ms = tspec.tv_nsec / 1000000;
    int us = tspec.tv_nsec / 1000 - ms * 1000;
    n += snprintf(buf + n, sizeof(buf) - n,
                  "%4d-%02d-%02d %02d:%02d:%02d.%03d%03d ", t.tm_year + 1900,
                  t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec, ms,
                  us);
  }

  if (format & MLOG_F_PID) {
    n += snprintf(buf + n, sizeof(buf) - n, "%d ", getpid());
  }

  if (format & MLOG_F_TID) {
    n += snprintf(buf + n, sizeof(buf) - n, "%d ", gettid());
  }

  if (format & MLOG_F_FILELINE) {
    n += snprintf(buf + n, sizeof(buf) - n, "%s:%d ", file, line);
  }

  if (format & MLOG_F_FUNC) {
    n += snprintf(buf + n, sizeof(buf) - n, "%s ", func);
  }

  va_list args;
  va_start(args, fmt);
  n += vsnprintf(buf + n, sizeof(buf) - n, fmt, args);
  if (n >= static_cast<int>(sizeof(buf))) {
    n = sizeof(buf) - 1;
  }
  va_end(args);

  buf[n++] = '\n';
  return Log(buf, n);
}

inline int CMLogger::Log(const char* buf, int len) {
  if (unlikely(!init)) return -1;

  if (unlikely(myts != mm->ts)) {
    sem_lock(semid);
    if (myts != mm->ts) {
      myts = mm->ts;
      int nfd = open(MakeName(myts).c_str(),
                     O_CREAT | O_RDWR | O_APPEND | O_LARGEFILE, 0666);
      if (nfd < 0)
        ;
      else if (fd < 0)
        fd = nfd;
      else {
        dup2(nfd, fd);
        close(nfd);
      }
    }
    sem_unlock(semid);
  }

  if (unlikely(mm->bytes >= size))  // Double Checked Locking
  {
    sem_lock(semid);
    // Maybe fd is old file, but check bytes is small (new file).
    if (mm->bytes >= size) {
      myts = time(nullptr);
      int nfd = open(MakeName(myts).c_str(),
                     O_CREAT | O_RDWR | O_APPEND | O_LARGEFILE, 0666);
      if (nfd < 0) {
        ;
      } else if (fd < 0) {
        fd = nfd;
      } else {
        // Confirm nfd != fd
        dup2(nfd, fd);  // Other threads may write to new file, but bytes added
                        // to old file.
        close(nfd);
      }

      mm->ts = myts;
      mm->bytes = 0;  // Other processes may hold old file, but check bytes is
                      // small (new file).
    }
    sem_unlock(semid);
  }

  int n;
  while ((n = write(fd, buf, len)) < 0 && errno == EINTR);
  if (n > 0) (void)__sync_add_and_fetch(&mm->bytes, n);

  return n;
}

}  // namespace mlog

inline int MLOG_INIT(mlog::CMLogger* logger, int level, unsigned format,
                     const char* dir, const char* prefix, unsigned long size) {
  mlog::getpname();
  return logger->Init(level, format, dir, prefix, size);
}

#define __FILENAME__ \
  (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)

#define MLOG(logger, level, fmt, y...)                                       \
  do {                                                                       \
    if (level > logger->Level()) break;                                      \
    logger->Log(level, STRIP_FILENAME(__FILENAME__), __LINE__, __FUNCTION__, \
                fmt, ##y);                                                   \
  } while (0)

#define MLOG_ERROR(logger, fmt, y...) \
  MLOG(logger, tylib::MLOG_LV_ERROR, fmt, ##y)
#define MLOG_NORMAL(logger, fmt, y...) \
  MLOG(logger, tylib::MLOG_LV_NORMAL, fmt, ##y)
#define MLOG_DEBUG(logger, fmt, y...) \
  MLOG(logger, tylib::MLOG_LV_DEBUG, fmt, ##y)
#define MLOG_TRACE(logger, fmt, y...) \
  MLOG(logger, tylib::MLOG_LV_TRACE, fmt, ##y)

inline mlog::CMLogger* mlog_default_logger() {
  static mlog::CMLogger inst;
  return &inst;
}

#define MLOG_DEF_LOGGER tylib::mlog_default_logger()

}  // namespace tylib

#define tylog(format, arg...) MLOG_NORMAL(MLOG_DEF_LOGGER, format, ##arg)

#endif  // TYLIB_LOG_LOG_H_
