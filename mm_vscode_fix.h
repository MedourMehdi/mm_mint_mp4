#include <sys/stat.h>
#include <mint/falcon.h>

#ifndef	__USE_POSIX
#define __USE_POSIX
#endif 
#ifndef	__USE_BSD
#define __USE_BSD
#endif 
#ifndef	__USE_GNU
#define __USE_GNU
#endif 

#define	INQUIRE		-1
#define	PLAY_ENABLE		1

int16_t st_Pgetpid(void);
int16_t st_Syield(void);
int32_t st_Fselect(u_int16_t timeout, int32_t *rfds, int32_t *wfds);


int32_t st_Supexec(int32_t atari_hardware_addr());
int32_t get200hz(void);
void    *st_mem_alloc(int32_t size);
void    *st_mem_calloc(int32_t size);
void    *st_mem_free(void *ptr);
void    *mm_mint_mp4_Snd_mem_alloc(int32_t size);
void    st_Vsync( int16_t nb_vsync );
int32_t st_Soundcmd( int16_t mode, int16_t data );
int32_t st_Sndstatus( int16_t reset );
int32_t st_Devconnect( int16_t src, int16_t dst, int16_t srcclk, int16_t prescale, int16_t protocol );
int32_t st_Setmode( int16_t mode );
int32_t st_Setbuffer( int16_t reg, void *begaddr, void *endaddr );
int32_t st_Setinterrupt( int16_t src_inter, int16_t cause );
int32_t st_Buffoper( int16_t mode );
int32_t st_Buffptr( int32_t *ptr );
int32_t st_Setmontracks( int16_t montrack );
int32_t st_Settracks( int16_t playtracks, int16_t rectracks );
void st_Xbtimer(int16_t timer, int16_t control, int16_t data, void(*vector)( ));
void st_Jenabint( int16_t timer );
void st_Jdisint( int16_t timer );
void st_enableTimerASei( void );
void st_Locksnd();
void st_Unlocksnd();