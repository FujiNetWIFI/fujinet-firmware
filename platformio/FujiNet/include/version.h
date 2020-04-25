#ifndef FNVERSION
#define FNVERSION

#ifdef GIT_SRC_REV
#define FUJINET_VERSION GIT_SRC_REV
#else
#define FUJINET_VERSION "Missing Git revision"
#endif

#endif // FNVERSION