#ifndef MCPE_FPS_H
#define MCPE_FPS_H

#ifdef __cplusplus
extern "C" {
#endif

// Call exactly once after each successfully presented game frame.
void mcpe_fps_on_frame(void);

// Snapshot values; returns 0 if no frame has been recorded yet.
int mcpe_fps_get_current(void);
int mcpe_fps_get_average(void);
int mcpe_fps_get_minimum(void);
int mcpe_fps_get_maximum(void);
void mcpe_fps_reset_stats(void);

#ifdef __cplusplus
}
#endif
#endif
