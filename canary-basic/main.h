/*********************************************************************
 *
 *  Yoctopuce Keep-it-stupid-simple canary demo software
 *
 *  Interface between main.c and the OS-specific code
 *
 *********************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

// Defined in main.c
int CanarySetup(void);
int CanaryRun(void);

#ifdef __cplusplus
}
#endif
