/*****************************************************************
*
*
*
*
*
*/

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Register all system functions
void register_sys(void);

// Register common system functions: "version", "restart", "free", "heap", "tasks"
void register_system_common(void);

// Register deep and light sleep functions
void register_system_sleep(void);

#ifdef __cplusplus
}
#endif
