
/* mm_vscode_fix prevent me to get vscode warnings about m68k registers */
#include "./mm_vscode_fix.h"

/*	Atari platforms frequencies
*	12584,25169,50352,0	; TT freq + delay = 0
*	12929,24585,49170,0	; Falcon freq + delay = 0
*	12300,24594,49165,0	; Aranym freq + delay = 0
*	12273,25335,50667,0	; Vampire SAGA
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>

#include <mint/osbind.h>
#include <mint/mintbind.h>
#include <mint/cookie.h>
#include <unistd.h>
#include <sys/stat.h>

#include <gem.h>
#include <gemx.h>

#include <png.h> /* For icons renderer */

#include <pthread.h>

#include <wels/codec_api.h> /* OpenH264 - video decoding */
// #include <fdk-aac/aacdecoder_lib.h> /* AAC audio decoding */
#include <neaacdec.h>
#include <mp4v2/mp4v2.h> /* MP4 container parsing */
/* 	
*		Autoscaling part: due to lake of performance on actual Atari platforms :
*		-	mm_mint_mp4_vid.limit_upscale = true
*		-	SIZER removed from wi_kind variable (see open_win function) 
*/
#include <libyuv.h>

#include <math.h>
#include <zita-resampler/vresampler.h> /* Resampling audio */

#define TRUE 1
#define FALSE 0

#ifndef UCHAR_MAX
#define UCHAR_MAX  255
#endif

#define MFDB_STRIDE(w)  (((w) + 15) & -16)

#define ALL_EVENTS (MU_MESAG|MU_BUTTON|MU_KEYBD|MU_TIMER|MU_M1|MU_M2)

#define LIMIT_UPSCALE	1

#define MIN_WIDTH  (2*gl_wbox)
#define MIN_HEIGHT (3*gl_hbox)

#define MIN(a,b) (a<=b?a:b)
#define MAX(a,b) (a>=b?a:b)
#define RESPECT_VIDEO_RATIO	TRUE

#define VIDEO_WIDTH 320
#define VIDEO_HEIGHT 240

#define PREFER_25KH

#define BYTES_TO_CHECK	8

#define COMPUTE_TRANSPARENCY(adjustedColor, opacity, foregroundColor, backgroundColor) { adjustedColor = opacity * foregroundColor + (1 - opacity) * backgroundColor; }

#define mul_3_fast(x) ((x << 1) + x)

#ifndef __GEMLIB_AES
extern int16_t gl_apid;
#else
int16_t gl_apid;
#endif

int16_t gl_hchar;
int16_t gl_wchar;
int16_t gl_wbox;
int16_t gl_hbox;						/* system sizes */

int16_t phys_handle;					/* physical workstation handle */
int16_t handle;							/* virtual workstation handle */
int16_t wi_handle;						/* window handle */
int16_t top_window;						/* handle of topped window */

int16_t wi_kind;

int16_t xdesk, ydesk, hdesk, wdesk;
int16_t xold, yold, hold, wold;
int16_t xwork, ywork, hwork, wwork;		/* desktop and work areas */
int16_t xext, yext, hext, wext;
int16_t msgbuff[8];						/* event message buffer */
int16_t keycode;						/* keycode returned by event-keyboard */
int16_t mx, my;							/* mouse x and y pos. */
int16_t mb, mc;							/* Mouse button/# clicks */
int16_t button_is_down;					/* button state tested for, UP/DOWN */
int16_t dummy_return_value;				/* dummy return variable */

int16_t cursor_is_hidden;				/* current state of cursor */
int16_t window_is_fulled;				/* current state of window */

int16_t work_in[11];					/* Input VDI parameters array */
int16_t work_out[57];					/* Output VDI parameters array */

int16_t work_out_extended[57];			/* Output extended VDI parameters array */

char current_path[256] = {'\0'};
char ico_path[256] = {'\0'};

MFDB mm_wi_mfdb = { 0 };
MFDB screen_mfdb = { 0 };
MFDB mm_control_bar_mfdb = { 0 };

struct stat	st_file_stat;
int16_t clock_unit = 5;
uint32_t time_start_witness, time_end_witness, time_start, time_end, duration;

bool app_end = false;

/****************************************************************/
/*  VIDEO		                                        */
/****************************************************************/

typedef struct struct_mm_vid {
	bool 		is_playing;
	bool		is_paused;
	bool		limit_upscale;
	double		video_ratio;
	uint32_t	frames_counter;
	uint32_t	frames_dropped;
	uint32_t	video_sample_offset;
	uint32_t	video_sample_max_size;
	uint8_t		*video_sample;
	const char*	track_media_data_name;
	uint32_t	track_frames;
	uint32_t	track_total_frames;
	uint32_t	track_max_frame_size;
	uint16_t	track_width;
	uint16_t	track_height;
	double		track_fps;
	double		computed_track_fps;
	uint32_t 	computed_current_time_sec;
	uint32_t	computed_total_time_sec;
	uint32_t	computed_left_time_sec;
	double		time_per_frame;
	double		track_total_duration;
	uint32_t	track_number;
	int32_t		trace_level;
	void		*pMP4_Handler;
	void		(*mm_mint_mp4_Vid_Open)();
	void 		(*mm_mint_mp4_Vid_Feed)();
	void 		(*mm_mint_mp4_Vid_Close)();
} struct_mm_vid;

static	struct_mm_vid 	mm_mint_mp4_vid;

static	ISVCDecoder		*pVidCodec_Handler = NULL;
static	SDecodingParam	pVidCodec_Param = { 0 };
static	SBufferInfo		*pDstBufInfo;

/****************************************************************/
/*  SOUND		                                        */
/****************************************************************/
typedef signed short SHORT;
typedef SHORT INT_PCM;

int16_t attenuation_left, attenuation_right;
int16_t loadNewSample;

static 	SndBufPtr ptr;

typedef struct struct_mm_snd {
    const char* filename;
    uint32_t   	filesize;
	bool 		is_playing;
	bool 		is_paused;
	bool 		is_muted;
	bool		real_time_resampling;
	bool		resampling_25kh;
	bool		resampling_48kh;
	uint16_t 	new_samplerate;
	uint16_t 	ori_samplerate;
	int32_t		atari_effective_sampleRate;
	double		resampling_ratio;
	double		track_total_duration;
	int32_t		track_channels;
	int32_t		track_sampleRate;
	int32_t		track_duration;
	int16_t		track_number;
	int16_t		clk_prescale;
	int8_t 		*pBuffer;
	int8_t 		*pPhysical;
	int8_t 		*pLogical;
	uint32_t 	bufferSize;
	uint32_t 	bytes_counter;
	uint32_t 	frames_counter;
	uint32_t	total_frames;
	uint16_t	track_max_frame_size;
	void		*pMP4_Handler;
	void		*pSndCodec_Handler;
	void 		(*mm_mint_mp4_Snd_Open)();
	void 		(*mm_mint_mp4_Snd_Feed_Buffer)( int8_t *pBuffer, uint32_t bufferSize );
	void 		(*mm_mint_mp4_Snd_Close)();
} struct_mm_snd;

struct_mm_snd  mm_mint_mp4_snd;
faacDecConfigurationPtr config;

uint8_t* pData = NULL;
/****************************************************************/
/*  RESAMPLING		                                */
/****************************************************************/
int FILTSIZE = 48;
VResampler *VR;

/****************************************************************/
/*  TIMER_A UTILITY ROUTINES.                                   */
/****************************************************************/

void __attribute__((interrupt)) timerA( void )
{
	loadNewSample = 1;
	*( (volatile unsigned char*)0xFFFFFA0FL ) &= ~( 1<<5 );	//	clear in service bit
}

/****************************************************************/
/*  SOUND FUNCTIONS DECLARATIONS.                                  	*/
/****************************************************************/

void mm_mint_mp4_Snd_LoadBuffer( int8_t* pBuffer, uint32_t bufferSize );

void mm_mint_mp4_Snd_Init();
void mm_mint_mp4_Snd_Set();
void mm_mint_mp4_Snd_Feed();
void mm_mint_mp4_Snd_UnSet();

int32_t mm_mint_mp4_Snd_Get_Playback_Position();

void mm_mint_mp4_Snd_PCM16_to_Float(float *outbuf, short *inbuf, int length);
void mm_mint_mp4_Snd_Float_to_PCM16(short *outbuf, float *inbuf, int length);

/****************************************************************/
/*  MAIN ROUTINES DECLARATIONS                                 	*/
/****************************************************************/

void mm_mint_mp4_Init();

void mm_mint_mp4_Open();
void mm_mint_mp4_Snd_MP4_Decode( int8_t *pBuffer, uint32_t bufferSize );
void mm_mint_mp4_Vid_MP4_Decode();
void mm_mint_mp4_Close();

void mm_mint_mp4_Snd_Close();
void mm_mint_mp4_Vid_Close();

void mm_mint_mp4_Snd_Mute();
void mm_mint_mp4_Snd_Pause();
void mm_mint_mp4_Snd_Restart();

void* mm_mint_mp4_Vid_Play( void* p_param );
void* mm_mint_mp4_Snd_Play( void* p_param );

uint32_t mm_mint_mp4_Get_First_VideoTrack( MP4FileHandle *p_this_MP4_Handler );
uint32_t mm_mint_mp4_Get_First_AudioTrack( MP4FileHandle *p_this_MP4_Handler );

void mm_mint_H264_Log_Callback( void *ctx, int level, const char* msg );

/****************************************************************/
/*  PNG ICONS DECLARATIONS                                 		*/
/****************************************************************/

typedef struct {
	const char* filename;
	uint16_t 	x;
	uint16_t 	y;
	uint16_t 	x2;
	uint16_t 	y2;
	MFDB 		mm_ico_mfdb;
} struct_mm_ico_png;

typedef struct {
    int16_t				icon_id;
	const char			*main_icon_path;
	const char			*mask_icon_path;
    struct_mm_ico_png	*main_icon;
    struct_mm_ico_png	*mask_icon;
    void				(*main_func)();
	int16_t				pos_x;
	int16_t 			pos_y;
	bool				icon_status;
} struct_mm_ico_list;

struct_mm_ico_png mm_ico_play_mfdb;
struct_mm_ico_png mm_ico_stop_mfdb;
struct_mm_ico_png mm_ico_pause_mfdb;
struct_mm_ico_png mm_ico_sound_mfdb;
struct_mm_ico_png mm_ico_mute_mfdb;

uint16_t	mm_ico_win_delta_y = 0;
int16_t		mm_ico_pxy_control_bar[4];

bool		mm_ico_need_to_reload_mfdb = true;

void mm_ico_Init();
void mm_ico_Decompress(const char *file_name, MFDB *foreground_mfdb);
void mm_ico_Load(int8_t *destination_buffer, struct_mm_ico_png *ico);

void mm_ico_Handle(int16_t mouse_x, int16_t mouse_y, int16_t mouse_button);
void mm_ico_Update_x(uint16_t index, int16_t new_pos_x, int16_t new_pos_y);

struct_mm_ico_list control_bar_list[] = {
	{	1,		"\\ico\\pause.png",			"\\ico\\play.png",	&mm_ico_play_mfdb,		&mm_ico_pause_mfdb, mm_mint_mp4_Snd_Pause,		32,		4 ,	false	},
	{	2,		"\\ico\\go_start.png",		NULL,				&mm_ico_stop_mfdb,		NULL,				mm_mint_mp4_Snd_Restart, 	70,		4 ,	false	},
	{	3,		"\\ico\\volume.png",			"\\ico\\mute.png",	&mm_ico_sound_mfdb,		&mm_ico_mute_mfdb, 	mm_mint_mp4_Snd_Mute,		106,	4 ,	false	},
	{	-1,		NULL,						NULL, 				NULL, 			 		NULL,				NULL,						0,		0 ,	0		},
};

int16_t mm_ico_padding_right = 72;

/****************************************************************/
/*  AES & MISC UTILITY DECLARATIONS                             */
/****************************************************************/

void *exec_eventloop( void* p_param );
void *st_Win_Redraw(void *p_param);

void *multi( void *p_param);
void st_Send_WM_REDRAW();

bool check_ext(const char *ext1, const char *ext2);
void stringtolower(char *ext);
void stringtoupper(char *ext);
const char *get_filename_ext(const char *filename);
void remove_quotes(char* s1, char* s2);

/****************************************************************/
/*  AES UTILITY ROUTINES                                        */
/****************************************************************/

void hide_mouse(void)
{
	if (!cursor_is_hidden)
	{
		graf_mouse(M_OFF, NULL);
		cursor_is_hidden = TRUE;
	}
}

void show_mouse(void)
{
	if (cursor_is_hidden)
	{
		graf_mouse(M_ON, NULL);
		cursor_is_hidden = FALSE;
	}
}

/****************************************************************/
/* Open virtual workstation                                     */
/****************************************************************/

void open_vwork(void)
{
	int16_t i;

	for (i = 0; i < 10; work_in[i++] = 1){
		work_in[10] = 2;
	}
	handle = phys_handle;
	v_opnvwk(work_in, &handle, work_out);
}

/****************************************************************/
/* Set clipping rectangle                                       */
/****************************************************************/

void set_clip(int16_t flag, int16_t x, int16_t y, int16_t w, int16_t h)
{
	int16_t clip[4];

	clip[0] = x;
	clip[1] = y;
	clip[2] = x + w - 1;
	clip[3] = y + h - 1;
	vs_clip(handle, flag, clip);
}

/****************************************************************/
/* open window                                                  */
/****************************************************************/
void open_window(int win_width, int win_height, const char *title)
{
	int16_t pxy[4];
	
	int16_t wi_kind;

	if( !mm_mint_mp4_vid.limit_upscale ){

		wi_kind = (MOVER|FULLER|SIZER|CLOSER|NAME);

	} else {
		wi_kind = (MOVER|CLOSER|NAME);
	}

	wi_handle = wind_create(wi_kind, xdesk, ydesk, wdesk, hdesk);
	wind_set_str(wi_handle, WF_NAME, title);
	wind_open(wi_handle, xdesk + 80, ydesk + 20, win_width, win_height);
	wind_set(wi_handle, WF_WORKXYWH, xdesk + 80, ydesk + 20, win_width, win_height);
	wind_get(wi_handle, WF_WORKXYWH, &xwork, &ywork, &wwork, &hwork);
	wind_get(wi_handle, WF_CURRXYWH, &xext, &yext, &wext, &hext);
	vsf_interior(handle, 0);

	pxy[0] = xwork;
	pxy[1] = ywork;
	pxy[2] = xwork + wwork;
	pxy[3] = ywork + hwork;
	set_clip(1, xwork, ywork, wwork, hwork);
	vr_recfl(handle, pxy);
	set_clip(0, xwork, ywork, wwork, hwork);

	mm_ico_pxy_control_bar[0] = xwork;
	mm_ico_pxy_control_bar[1] = ywork + hwork - mm_ico_win_delta_y;
	mm_ico_pxy_control_bar[2] = xwork + wwork;
	mm_ico_pxy_control_bar[3] = ywork + hwork;
}

/****************************************************************/
/* Application loop                                             */
/****************************************************************/

void *st_Win_Redraw( void *p_param){
	GRECT rect;
	int16_t pxy_array[8];
	hide_mouse( );
	wind_update(BEG_UPDATE);
	wind_get(wi_handle, WF_FIRSTXYWH, &rect.g_x, &rect.g_y, &rect.g_w, &rect.g_h);
	while(rect.g_h  != 0 && rect.g_w != 0){
		if ( rc_intersect((GRECT *)&msgbuff[4], &rect) ){

			pxy_array[0] = rect.g_x - xwork;
			pxy_array[1] = rect.g_y - ywork;
			pxy_array[2] = pxy_array[0] + rect.g_w;
			pxy_array[3] = pxy_array[1] + rect.g_h;

			grect_to_array(&rect,&pxy_array[4]);

			set_clip(1, rect.g_x, rect.g_y, rect.g_w, rect.g_h);
			vr_recfl(handle, &pxy_array[4]);
			vro_cpyfm(handle, S_ONLY, pxy_array, &mm_wi_mfdb, &screen_mfdb);
		}
		wind_get(wi_handle,WF_NEXTXYWH,&rect.g_x, &rect.g_y, &rect.g_w, &rect.g_h);
	}
	wind_update(END_UPDATE);
	graf_mouse(ARROW,0L);
	show_mouse();
	return NULL;
}

void st_Send_WM_REDRAW(){
	int16_t my_win_message[8];
	my_win_message[0] = WM_REDRAW;
	my_win_message[1] = gl_apid;
	my_win_message[2] = 0;
	my_win_message[3] = wi_handle;
	my_win_message[4] = xwork;
	my_win_message[5] = ywork;
	my_win_message[6] = wwork;
	my_win_message[7] = hwork;
	appl_write(gl_apid, 16, &my_win_message);
}

/****************************************************************/
/* Dispatches all application tasks                             */
/****************************************************************/

void* exec_eventloop(void* p_param){
	while ( app_end != true )
	{
		multi(NULL);
		pthread_yield_np();
	}
	return NULL;
}

void *multi( void *p_param )
{

	int16_t event;
		event = evnt_multi(MU_MESAG | MU_BUTTON | MU_KEYBD | MU_TIMER,
				256 | 2, 3, button_is_down,
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0, msgbuff, 0, &mx, &my, &mb, &mc, &keycode, &dummy_return_value);

		wind_update(TRUE);

		if (event & MU_MESAG)
		{
			switch (msgbuff[0])
			{
			case WM_CLOSED:
				app_end = true;
				break;
			case WM_REDRAW:
				if(mm_mint_mp4_vid.is_playing == false){
					st_Win_Redraw(NULL);
				}
				break;
			case WM_NEWTOP:
				break;
			case WM_TOPPED:

				wind_set(wi_handle, WF_TOP, 0, 0, 0, 0);

				break;
			case WM_SIZED:
				if(mm_mint_mp4_vid.track_number > 0 ){
				msgbuff[7] = RESPECT_VIDEO_RATIO == TRUE ? msgbuff[6] / mm_mint_mp4_vid.video_ratio : msgbuff[7] ;
				int16_t tmp_win_area[8];
				wind_calc(WC_WORK,wi_kind, msgbuff[4], msgbuff[5], msgbuff[6], msgbuff[7],&tmp_win_area[0],&tmp_win_area[1],&tmp_win_area[2],&tmp_win_area[3]);
				if(mm_mint_mp4_vid.limit_upscale == true){
					tmp_win_area[2] = MIN(mm_mint_mp4_vid.track_width, tmp_win_area[2]);
					tmp_win_area[3] = MIN(mm_mint_mp4_vid.track_height, tmp_win_area[3]);
				}
				wind_calc(WC_BORDER,wi_kind, tmp_win_area[0], tmp_win_area[1], tmp_win_area[2], tmp_win_area[3],&tmp_win_area[0],&tmp_win_area[1],&tmp_win_area[2],&tmp_win_area[3]);
				wind_set(msgbuff[3],WF_CURRXYWH, tmp_win_area[0], tmp_win_area[1], tmp_win_area[2], tmp_win_area[3]);
				} else {
					wind_set(wi_handle, WF_CURRXYWH, msgbuff[4], msgbuff[5], msgbuff[6], msgbuff[7]);
				}
				wind_get(wi_handle, WF_WORKXYWH, &xwork, &ywork, &wwork, &hwork);
				wind_get(wi_handle, WF_CURRXYWH, &xext, &yext, &wext, &hext);

				mm_ico_pxy_control_bar[0] = xwork;
				mm_ico_pxy_control_bar[1] = ywork + hwork - mm_ico_win_delta_y;
				mm_ico_pxy_control_bar[2] = xwork + wwork;
				mm_ico_pxy_control_bar[3] = ywork + hwork;

				mm_ico_Update_x(2, wwork - mm_ico_padding_right, -1);

				st_Send_WM_REDRAW();

				break;
			case WM_MOVED:

				if (msgbuff[6] < MIN_WIDTH)
					msgbuff[6] = MIN_WIDTH;
				if (msgbuff[7] < MIN_HEIGHT)
					msgbuff[7] = MIN_HEIGHT;
				wind_set(wi_handle, WF_CURRXYWH, msgbuff[4], msgbuff[5], msgbuff[6], msgbuff[7]);
				wind_get(wi_handle, WF_WORKXYWH, &xwork, &ywork, &wwork, &hwork);
				wind_get(wi_handle, WF_CURRXYWH, &xext, &yext, &wext, &hext);

				mm_ico_pxy_control_bar[0] = xwork;
				mm_ico_pxy_control_bar[1] = ywork + hwork - mm_ico_win_delta_y;
				mm_ico_pxy_control_bar[2] = xwork + wwork;
				mm_ico_pxy_control_bar[3] = ywork + hwork;

				break;
			case WM_FULLED:
				break;
			default:
				break;
			}
		}

		if (event & MU_BUTTON)
		{
			if( mb == 2 && xwork < mx < wwork && ywork < my < hwork && mm_mint_mp4_vid.track_number > 0)
			{
				mm_ico_win_delta_y = mm_ico_win_delta_y > 0 ? 0 : 40;
				mm_ico_pxy_control_bar[0] = xwork;
				mm_ico_pxy_control_bar[2] = xwork + wwork;
				mm_ico_pxy_control_bar[3] = ywork + hwork;
				mm_ico_pxy_control_bar[1] = mm_ico_pxy_control_bar[3] - mm_ico_win_delta_y;
			}
			if( mb == 1 ){
				if(mm_ico_win_delta_y > 0 && xwork < mx < wwork && ywork < my < hwork )
				{
					mm_ico_Handle(mx, my, mb);
					if(!mm_mint_mp4_vid.is_playing){
						mm_ico_Handle(0, 0, 0);
						st_Send_WM_REDRAW();
					}
				}
			}

		}
		wind_update(FALSE);
		return NULL;
}

void mm_ico_Update_x(uint16_t index, int16_t new_pos_x, int16_t new_pos_y){
		control_bar_list[ index ].pos_x = new_pos_x < 0 ? control_bar_list[ index ].pos_x : new_pos_x;
		control_bar_list[ index ].pos_y = new_pos_y < 0 ? control_bar_list[ index ].pos_y : new_pos_y;
		control_bar_list[ index ].main_icon->x = control_bar_list[ index ].pos_x;
		control_bar_list[ index ].main_icon->y = control_bar_list[ index ].pos_y;
		control_bar_list[ index ].main_icon->x2 = control_bar_list[ index ].main_icon->x + control_bar_list[ index ].main_icon->mm_ico_mfdb.fd_w;
		control_bar_list[ index ].main_icon->y2 = control_bar_list[ index ].main_icon->y + control_bar_list[ index ].main_icon->mm_ico_mfdb.fd_h;
		if(control_bar_list[ index ].mask_icon_path != NULL){
			control_bar_list[ index ].mask_icon->filename = control_bar_list[ index ].mask_icon_path;
			control_bar_list[ index ].mask_icon->x = control_bar_list[ index ].pos_x;
			control_bar_list[ index ].mask_icon->y = control_bar_list[ index ].pos_y;
			control_bar_list[ index ].mask_icon->x2 = control_bar_list[ index ].mask_icon->x + control_bar_list[ index ].mask_icon->mm_ico_mfdb.fd_w;
			control_bar_list[ index ].mask_icon->y2 = control_bar_list[ index ].mask_icon->y + control_bar_list[ index ].mask_icon->mm_ico_mfdb.fd_h;
		}

}

void mm_ico_Init(){

	uint16_t i = 0;

	while(control_bar_list[ i ].icon_id > 0) {
		control_bar_list[ i ].main_icon->x = control_bar_list[ i ].pos_x;
		control_bar_list[ i ].main_icon->y = control_bar_list[ i ].pos_y;
		control_bar_list[ i ].main_icon->filename = control_bar_list[ i ].main_icon_path;

		int16_t ico_path_len = strlen(ico_path);
		if( ico_path_len < 1){
			strcpy(ico_path, current_path);
		}
		char this_ico_path[ico_path_len + strlen(control_bar_list[ i ].main_icon_path) + 1] = {'\0'};
		strcpy(this_ico_path, ico_path);
		strcat(this_ico_path, control_bar_list[ i ].main_icon_path);
		printf("this_ico_path %s\n", this_ico_path);
		mm_ico_Decompress(this_ico_path, &control_bar_list[ i ].main_icon->mm_ico_mfdb);
		control_bar_list[ i ].main_icon->x2 = control_bar_list[ i ].main_icon->x + control_bar_list[ i ].main_icon->mm_ico_mfdb.fd_w;
		control_bar_list[ i ].main_icon->y2 = control_bar_list[ i ].main_icon->y + control_bar_list[ i ].main_icon->mm_ico_mfdb.fd_h;
		if(control_bar_list[ i ].mask_icon_path != NULL){
			control_bar_list[ i ].mask_icon->filename = control_bar_list[ i ].mask_icon_path;
			control_bar_list[ i ].mask_icon->x = control_bar_list[ i ].pos_x;
			control_bar_list[ i ].mask_icon->y = control_bar_list[ i ].pos_y;
			strcpy(this_ico_path, ico_path);
			strcat(this_ico_path, control_bar_list[ i ].mask_icon_path);			
			mm_ico_Decompress(this_ico_path, &control_bar_list[ i ].mask_icon->mm_ico_mfdb);
			control_bar_list[ i ].mask_icon->x2 = control_bar_list[ i ].mask_icon->x + control_bar_list[ i ].mask_icon->mm_ico_mfdb.fd_w;
			control_bar_list[ i ].mask_icon->y2 = control_bar_list[ i ].mask_icon->y + control_bar_list[ i ].mask_icon->mm_ico_mfdb.fd_h;
		}

		i++;
	}
}

void mm_ico_Load(MFDB *dest_mfdb, struct_mm_ico_png *ico){

	uint8_t		*destination_buffer = (uint8_t *)dest_mfdb->fd_addr;
	uint8_t		*ico_buffer = (uint8_t*)ico->mm_ico_mfdb.fd_addr;
	int16_t		width = ico->mm_ico_mfdb.fd_w;
	int16_t		height = ico->mm_ico_mfdb.fd_h;
	int16_t		x, y;
	uint32_t	m, n, i, j, k;
	uint8_t		fg_r, fg_g, fg_b, a, r, g, b, red, green, blue;

	for(y = 0; y < height; y++){
		m = (width * y);
		n = (dest_mfdb->fd_w * y );
		for(x = 0; x < width; x++){
			j = ((m + x) << 2);
			i = (n + x + ico->x);
			switch (dest_mfdb->fd_nplanes)
			{
			case 16:
				i = i << 1;
				k = i;
				break;
			case 24:
				i = (i << 1) + i;
				k = i - 1;
				break;
			case 32:
				i = i << 2;
				k = i;
				break;							
			default:
				break;
			}
			a = ico_buffer[j++];
			r = ico_buffer[j++];
			g = ico_buffer[j++];
			b = ico_buffer[j++];
			switch (a)
			{
			case 255:
				fg_r	= r;
				fg_g	= g;
				fg_b 	= b;
				break;
			case 0:
				fg_r	= destination_buffer[k + 1];
				fg_g	= destination_buffer[k + 2];
				fg_b	= destination_buffer[k + 3];
				break;			
			default:
				COMPUTE_TRANSPARENCY(fg_r, a/255, r, destination_buffer[k + 1]);
				COMPUTE_TRANSPARENCY(fg_g, a/255, g, destination_buffer[k + 2]);
				COMPUTE_TRANSPARENCY(fg_b, a/255, b, destination_buffer[k + 3]);			
				break;
			}

			switch (dest_mfdb->fd_nplanes)
			{
			case 16:
				if(a == 255){
					u_int32_t pix32 = (a << 24) | ((fg_r & 0xFF) << 16) | ((fg_g & 0xFF) << 8) | (fg_b & 0xFF);
					u_int16_t* pix16 = (u_int16_t*)&destination_buffer[i];
					*pix16 = ARGB_to_RGB565((u_int8_t*)&pix32);
				}
				i += 2;
				break;
			case 24:
				destination_buffer[i++] = fg_r;
				destination_buffer[i++] = fg_g;
				destination_buffer[i++] = fg_b;
				break;
			case 32:
				destination_buffer[i++] = a;
				destination_buffer[i++] = fg_r;
				destination_buffer[i++] = fg_g;
				destination_buffer[i++] = fg_b;
				break;	
			default:
				break;
			}

		}
	}
}

void mm_ico_Decompress(const char *file_name, MFDB *foreground_mfdb)
{
	FILE *fp;

	int x, y;
	int width, height, channels, interlace;
	int number_of_passes;

	png_byte color_type;
	png_byte bit_depth;

	png_structp png_ptr;
	png_infop info_ptr;
	
	png_bytep * row_pointers;

	uint8_t header[BYTES_TO_CHECK];

	fp = fopen(file_name, "rb");
	if (!fp)
			form_alert(1, "[1][File could not be opened for reading][Okay]");
	fread(header, 1, 8, fp);


	if (png_sig_cmp((png_const_bytep)header, 0, BYTES_TO_CHECK)){
		form_alert(1, "[1][This file is not recognized as a PNG file][Okay]");
		return;
	}

	png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (!png_ptr){
		form_alert(1, "[1][png_create_read_struct failed][Okay]");
		return;
	}
	info_ptr = png_create_info_struct(png_ptr);
	if (!info_ptr){
		form_alert(1, "[1][png_create_info_struct failed][Okay]");
		return;
	}

	png_init_io(png_ptr, fp);

	png_set_sig_bytes(png_ptr, BYTES_TO_CHECK);
	png_read_info(png_ptr, info_ptr);

	color_type = png_get_color_type(png_ptr, info_ptr);
	bit_depth = png_get_bit_depth(png_ptr, info_ptr);

    if (bit_depth == 16) {
        png_set_strip_16(png_ptr);
    }
    if (color_type == PNG_COLOR_TYPE_PALETTE) {
        png_set_palette_to_rgb(png_ptr);
    }
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) {
        png_set_expand_gray_1_2_4_to_8(png_ptr);
    }
    if(png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS)){
		png_set_tRNS_to_alpha(png_ptr);
	}
    if(color_type == PNG_COLOR_TYPE_RGB ||
       color_type == PNG_COLOR_TYPE_GRAY ||
       color_type == PNG_COLOR_TYPE_PALETTE) {
        png_set_filler(png_ptr, 0xFF, PNG_FILLER_AFTER);
    }
    if(color_type == PNG_COLOR_TYPE_GRAY ||
       color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
        png_set_gray_to_rgb(png_ptr);
    }
    if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS) != 0){
        png_set_tRNS_to_alpha(png_ptr);
	}

    png_read_update_info(png_ptr, info_ptr);

	width = png_get_image_width(png_ptr, info_ptr);
	height = png_get_image_height(png_ptr, info_ptr);
	color_type = png_get_color_type(png_ptr, info_ptr);
	bit_depth = png_get_bit_depth(png_ptr, info_ptr);
	channels = png_get_channels( png_ptr, info_ptr);

	row_pointers = (png_bytep *)st_mem_alloc(sizeof(png_bytep) * height);
	for (y=0; y<height; y++)
				row_pointers[y] = (png_byte *)st_mem_alloc(png_get_rowbytes(png_ptr,info_ptr));

	png_read_image(png_ptr, row_pointers);

	int16_t i, j, k;

	uint16_t a, r, g, b, red, green, blue;
	int16_t nb_components_original = channels;
	int16_t nb_components_32bits = 4;

	int16_t nb_px_needed_to_resize = 0;
	while ((width + nb_px_needed_to_resize ) % 16 != 0)
	{
		nb_px_needed_to_resize++;
	}

	uint32_t origninal_size_in_bytes = width * height * nb_components_original;
    uint32_t destination_size_in_bytes = (width + nb_px_needed_to_resize) * height * nb_components_32bits;

	uint8_t *destination_buffer;
    destination_buffer = (uint8_t*)st_mem_alloc(destination_size_in_bytes + nb_px_needed_to_resize);

	if(channels == 4){
		for (y = (height - 1); y != -1; y--)
		{
			for (x = 0; x < width; x++)
			{
				i = (x + y * (width + nb_px_needed_to_resize)) * nb_components_32bits;
				r = row_pointers[y][x * nb_components_original];
				g = row_pointers[y][(x * nb_components_original) + 1];
				b = row_pointers[y][(x * nb_components_original) + 2];
				a = row_pointers[y][(x * nb_components_original) + 3];

				red   = r;
				green = g;
				blue  = b;

				destination_buffer[i++] = a;
				destination_buffer[i++] = red;
				destination_buffer[i++] = green;
				destination_buffer[i++] = blue;
			}
			for(k = nb_px_needed_to_resize; k > 0; k--){
				destination_buffer[i++] = 0x00;
				destination_buffer[i++] = 0xFF;
				destination_buffer[i++] = 0xFF;
				destination_buffer[i++] = 0xFF;
			}
		}
	}
	
    png_read_end(png_ptr, info_ptr);
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);

	fclose(fp);
	for (y=0; y<height; y++){
		st_mem_free(row_pointers[y]);
	}
	st_mem_free(row_pointers);

	foreground_mfdb->fd_w = width + nb_px_needed_to_resize;
	foreground_mfdb->fd_h = height;
	foreground_mfdb->fd_wdwidth = MFDB_STRIDE( foreground_mfdb->fd_w ) >> 4;
	foreground_mfdb->fd_nplanes = work_out_extended[4];
	foreground_mfdb->fd_addr = (char *)destination_buffer;;
	foreground_mfdb->fd_stand = 0;
}

void mm_ico_Handle(int16_t mouse_x, int16_t mouse_y, int16_t mouse_button){
	uint16_t i = 0;
	struct_mm_ico_png *ico_ptr;

	if(mouse_button == 1){
		while(control_bar_list[ i ].icon_id > 0) {
			if( ( mouse_x > mm_ico_pxy_control_bar[0] + control_bar_list[ i ].main_icon->x ) &&
			 mouse_x < ( mm_ico_pxy_control_bar[0] + control_bar_list[ i ].main_icon->x2 ) && ( mouse_y > mm_ico_pxy_control_bar[1] + control_bar_list[ i ].main_icon->y ) &&
			 (mouse_y < mm_ico_pxy_control_bar[1] + control_bar_list[ i ].main_icon->y2) )
			{
				control_bar_list[ i ].main_func();
				if(control_bar_list[ i ].mask_icon != NULL){
					if( control_bar_list[ i ].icon_status == true ) {
						ico_ptr = control_bar_list[ i ].main_icon;
						control_bar_list[ i ].icon_status = false;
					} else {
						ico_ptr = control_bar_list[ i ].mask_icon;
						control_bar_list[ i ].icon_status = true;
					}
				}
				break;
			}
			i++;
		}

	} else {
		while(control_bar_list[ i ].icon_id > 0) {
				if( control_bar_list[ i ].icon_status == true ) {
					ico_ptr = control_bar_list[ i ].mask_icon;
				} else {
					ico_ptr = control_bar_list[ i ].main_icon;
				}
			mm_ico_Load( &mm_control_bar_mfdb, ico_ptr );
			i++;
		}
	}
}

/****************************************************************/
/*            Application Init. Until First Event_Multi         */
/****************************************************************/
int main(int argc, char *argv[])
{	
	pthread_t thread_video, thread_sound, thread_eventloop;
	int  iret_video, iret_sound, iret_eventloop;
    FILE* p_file;

	st_Get_Current_Dir(current_path);
	printf("current_path %s\n", current_path);
	char* this_file = (char*)st_mem_alloc(256);
	memset(this_file, 0, 128);


	if (argc > 1){
		for(int16_t i = 1; i < argc; i++) {
				strcat(this_file, argv[i]);
				if(i < (argc - 1)){strcat(this_file, " ");}
		}
		remove_quotes(this_file, this_file);
		mm_mint_mp4_snd.filename = this_file;

	}
	else{
		form_alert(1, "[1][Provide a file as argument][Okay]");
		return 1;
	}

	const char *file_extension = get_filename_ext(mm_mint_mp4_snd.filename);
	if (!check_ext(file_extension, "MP4") && !check_ext(file_extension, "M4A")){
		printf("File ext. %s\n", file_extension);
		form_alert(1, "[1][Only MP4 with H264 & AAC encodings are supported][Okay]");
		return 1;
	}

	p_file = fopen(mm_mint_mp4_snd.filename, "rb");
	fstat(fileno(p_file), &st_file_stat);
    mm_mint_mp4_snd.filesize = st_file_stat.st_size;
    fclose(p_file);

	appl_init();
	phys_handle = graf_handle(&gl_wchar, &gl_hchar, &gl_wbox, &gl_hbox);
	wind_get(0, WF_WORKXYWH, &xdesk, &ydesk, &wdesk, &hdesk);
	open_vwork();
	vq_extnd(handle,1,work_out_extended);
	/* Set Text color to Blue */
	vst_color(handle, 4);

	graf_mouse(ARROW, NULL);

	cursor_is_hidden = FALSE;
	window_is_fulled = FALSE;

	iret_eventloop = pthread_create( &thread_eventloop, NULL, &exec_eventloop, NULL);

	mm_ico_Init();

	mm_mint_mp4_Snd_Init();

	if(mm_mint_mp4_snd.track_number > 0 && !app_end) {
		mm_mint_mp4_Snd_Set();
	}

	/*
	*	Keep video first then audio
	*/ 

	if(mm_mint_mp4_vid.track_number > 0) {
		iret_video = pthread_create( &thread_video, NULL, &mm_mint_mp4_Vid_Play, NULL);
	}

	if(mm_mint_mp4_snd.track_number > 0) {
		iret_sound = pthread_create( &thread_sound, NULL, &mm_mint_mp4_Snd_Play, NULL);
	}

	pthread_join( thread_eventloop, NULL);

	if(mm_mint_mp4_vid.track_number > 0) {
		pthread_join( thread_video, NULL);
		mm_mint_mp4_Vid_Close();
	}

	if(mm_mint_mp4_snd.track_number > 0) {
		pthread_join( thread_sound, NULL);
		mm_mint_mp4_Snd_UnSet();
	}

	mm_mint_mp4_Close();

	/* Reset Text color to Black */
	vst_color(handle, 1);

	st_mem_free(this_file);

	wind_close(wi_handle);
	wind_delete(wi_handle);
	v_clsvwk(handle);
	appl_exit();

	return 0;
}

/****************************************************************/
/*            Sound Routines							        */
/****************************************************************/

void mm_mint_mp4_Snd_LoadBuffer( int8_t* pBuffer, uint32_t bufferSize ){
		mm_mint_mp4_snd.mm_mint_mp4_Snd_Feed_Buffer( pBuffer, bufferSize);
}

void mm_mint_mp4_Snd_Init(){
	mm_mint_mp4_Init();
    st_Locksnd();
	attenuation_left = (short)st_Soundcmd( LTATTEN, SND_INQUIRE );
	attenuation_right = (short)st_Soundcmd( RTATTEN, SND_INQUIRE );

	mm_mint_mp4_snd.mm_mint_mp4_Snd_Open();
}

void mm_mint_mp4_Snd_Set(){

	if(mm_mint_mp4_snd.track_sampleRate < 1 || mm_mint_mp4_snd.track_sampleRate > 100000){
		printf("ERROR: Sample rate %ld", mm_mint_mp4_snd.track_sampleRate);
	}
	mm_mint_mp4_snd.bufferSize = mm_mint_mp4_snd.atari_effective_sampleRate << 2;	// 2 channels * 16 bit * FREQ Hz * 1 second
	mm_mint_mp4_snd.pBuffer = NULL;
	mm_mint_mp4_snd.pBuffer = (int8_t*)mm_mint_mp4_Snd_mem_alloc(mm_mint_mp4_snd.bufferSize << 1);
	if( mm_mint_mp4_snd.pBuffer == NULL ){
		printf("ERROR: Malloc snd.pBuffer\n");
	}
	mm_mint_mp4_snd.pPhysical = mm_mint_mp4_snd.pBuffer;
	mm_mint_mp4_snd.pLogical = mm_mint_mp4_snd.pBuffer + mm_mint_mp4_snd.bufferSize;

	mm_mint_mp4_Snd_LoadBuffer( mm_mint_mp4_snd.pPhysical, mm_mint_mp4_snd.bufferSize );
    mm_mint_mp4_Snd_LoadBuffer( mm_mint_mp4_snd.pLogical, mm_mint_mp4_snd.bufferSize );

	st_Sndstatus( SND_RESET );

	int32_t curadder = st_Soundcmd(ADDERIN, INQUIRE);
	int32_t curadc = st_Soundcmd(ADCINPUT, INQUIRE);

	st_Soundcmd(ADCINPUT, 0);

    if( st_Setmode( MODE_STEREO16 ) != 0 ){
		printf("ERROR: Can not set MODE_STEREO16\n");
	}

	st_Devconnect( DMAPLAY, DAC, CLK25M, mm_mint_mp4_snd.clk_prescale, NO_SHAKE );

	if( st_Setbuffer( SR_PLAY, mm_mint_mp4_snd.pPhysical, mm_mint_mp4_snd.pPhysical + mm_mint_mp4_snd.bufferSize ) != 0 ){
		printf("ERROR st_Setbuffer\n");
	}

	if( st_Setinterrupt( SI_TIMERA, SI_PLAY ) != 0 ) {
		printf("ERROR: st_Setinterrupt\n");
	}

	st_Xbtimer( XB_TIMERA, 1<<3, 1, timerA );	// event count mode, count to '1'
	st_enableTimerASei();
	st_Jenabint( MFP_TIMERA );

	if( st_Buffoper( SB_PLA_ENA | SB_PLA_RPT ) != 0 ){
		printf("ERROR: st_Buffoper\n");
	}

	loadNewSample = 1;
}

void mm_mint_mp4_Snd_Feed()
{
	if( loadNewSample )
	{
		/* fill in logical buffer */
		mm_mint_mp4_Snd_LoadBuffer( mm_mint_mp4_snd.pLogical, mm_mint_mp4_snd.bufferSize );

		/* swap buffers (makes logical buffer physical) */
		int8_t* tmp = mm_mint_mp4_snd.pPhysical;
		mm_mint_mp4_snd.pPhysical = mm_mint_mp4_snd.pLogical;
		mm_mint_mp4_snd.pLogical = tmp;

		/* set physical buffer for the next frame */
		st_Setbuffer( SR_PLAY, mm_mint_mp4_snd.pPhysical, mm_mint_mp4_snd.pPhysical + mm_mint_mp4_snd.bufferSize );

		loadNewSample = 0;
	}

}

void mm_mint_mp4_Snd_UnSet(){
	
	st_Buffoper( 0x00 );	// disable playback
	st_Jdisint( MFP_TIMERA );
	mm_mint_mp4_snd.mm_mint_mp4_Snd_Close();
	mm_mint_mp4_Snd_mem_free( mm_mint_mp4_snd.pBuffer );
	mm_mint_mp4_snd.pBuffer = NULL;

	mm_mint_mp4_snd.is_playing = false;
	mm_mint_mp4_vid.is_playing = false;

	st_Soundcmd( LTATTEN, attenuation_left );
	st_Soundcmd( RTATTEN, attenuation_right );

	st_Unlocksnd(); 
}

void mm_mint_mp4_Snd_Pause(){
	mm_mint_mp4_snd.is_paused = mm_mint_mp4_snd.is_paused == true ? false : true;
	mm_mint_mp4_snd.is_playing = mm_mint_mp4_snd.is_paused == true ? false : true;
	if(mm_mint_mp4_vid.track_number > 0){
		mm_mint_mp4_vid.is_paused = mm_mint_mp4_snd.is_paused == true ? true : false;
		mm_mint_mp4_vid.is_playing = mm_mint_mp4_vid.is_paused == true ? false : true;
	}
	if( mm_mint_mp4_snd.is_paused == true ) {
		memset( mm_mint_mp4_snd.pPhysical, 0, mm_mint_mp4_snd.bufferSize );
		memset( mm_mint_mp4_snd.pLogical, 0, mm_mint_mp4_snd.bufferSize );
	}
}

void mm_mint_mp4_Snd_Restart(){
	mm_mint_mp4_snd.bytes_counter = 0;
	mm_mint_mp4_snd.frames_counter = 1;
	mm_mint_mp4_Snd_LoadBuffer( mm_mint_mp4_snd.pPhysical, mm_mint_mp4_snd.bufferSize );
    mm_mint_mp4_Snd_LoadBuffer( mm_mint_mp4_snd.pLogical, mm_mint_mp4_snd.bufferSize );
	loadNewSample = 1;
	time_start_witness = mm_mint_mp4_Snd_Get_Playback_Position();
	mm_mint_mp4_vid.frames_counter = 1;
}

void mm_mint_mp4_Snd_Mute(){

	mm_mint_mp4_snd.is_muted = mm_mint_mp4_snd.is_muted == true ? false : true;

	if( mm_mint_mp4_snd.is_muted == true && mm_mint_mp4_snd.is_playing == true )
	{
		memset( mm_mint_mp4_snd.pPhysical, 0, mm_mint_mp4_snd.bufferSize );
	}
}

int32_t mm_mint_mp4_Snd_Get_Playback_Position(){
	SndBufPtr local_ptr;
	st_Buffptr( (int32_t*)&local_ptr );
	int32_t current_position = local_ptr.play - (char*)mm_mint_mp4_snd.pBuffer;
	current_position = current_position > mm_mint_mp4_snd.bufferSize ? current_position - mm_mint_mp4_snd.bufferSize : current_position;
	return current_position >> 2;
}

void mm_mint_mp4_Snd_Float_to_PCM16(short *outbuf, float *inbuf, int length)
{
    int   count;

    const float mul = (32768.0f);
    for (count = 0; count <= length; count++) {
		int32_t tmp = (int32_t)(mul * inbuf[count]);
		tmp = MAX( tmp, -32768 ); // CLIP < 32768
		tmp = MIN( tmp, 32767 );  // CLIP > 32767
		outbuf[count] = tmp;
    }
}

void mm_mint_mp4_Snd_PCM16_to_Float(float *outbuf, short *inbuf, int length)
{
    int   count;

    const float div = (1.0f/32768.0f);
    for (count = 0; count <= length; count++) {
	outbuf[count] = div * (float) inbuf[count];
    }
}

/****************************************************************/
/*            Main Application Routines					        */
/****************************************************************/

void mm_mint_mp4_Init(){
    mm_mint_mp4_snd.mm_mint_mp4_Snd_Open = mm_mint_mp4_Open;
    mm_mint_mp4_snd.mm_mint_mp4_Snd_Feed_Buffer = mm_mint_mp4_Snd_MP4_Decode;
    mm_mint_mp4_snd.mm_mint_mp4_Snd_Close = mm_mint_mp4_Snd_Close;
	mm_mint_mp4_vid.mm_mint_mp4_Vid_Open = mm_mint_mp4_snd.mm_mint_mp4_Snd_Open;
	mm_mint_mp4_vid.mm_mint_mp4_Vid_Feed = mm_mint_mp4_Vid_MP4_Decode;
	mm_mint_mp4_vid.mm_mint_mp4_Vid_Close = mm_mint_mp4_Vid_Close;

#ifdef PREFER_25KH
	/* If we prefer downscaling instead upscaling */
	mm_mint_mp4_snd.resampling_25kh = true;
	mm_mint_mp4_snd.resampling_48kh = false;
#endif

	mm_mint_mp4_snd.track_number = 0;
	mm_mint_mp4_vid.track_number = 0;

	mm_mint_mp4_vid.limit_upscale = LIMIT_UPSCALE;

	mm_mint_mp4_snd.is_playing = false;
	mm_mint_mp4_snd.is_paused = false;
	mm_mint_mp4_vid.is_paused = false;
	mm_mint_mp4_snd.is_muted = false;

}

uint32_t mm_mint_mp4_Get_First_VideoTrack( MP4FileHandle *p_this_MP4_Handler ) {
    uint32_t trackCount = MP4GetNumberOfTracks(p_this_MP4_Handler, NULL, 0);
    if (trackCount == 0) {
        return 0;
    }
    for (uint16_t i = 0; i < trackCount; i++)
    {
        MP4TrackId  id = MP4FindTrackId(p_this_MP4_Handler, i, NULL, 0);
        const char* trackType = MP4GetTrackType(p_this_MP4_Handler, id);
        if (MP4_IS_VIDEO_TRACK_TYPE(trackType))
        {
			return id;
		}
    }
    return 0;
}

uint32_t mm_mint_mp4_Get_First_AudioTrack(MP4FileHandle *p_this_MP4_Handler) {

    uint32_t trackCount = MP4GetNumberOfTracks(p_this_MP4_Handler, NULL, 0);

    if (trackCount == 0) {
        return 0;
    }

    for (uint16_t i = 0; i < trackCount; i++)
    {
        MP4TrackId  id = MP4FindTrackId(p_this_MP4_Handler, i, NULL, 0);
        const char* trackType = MP4GetTrackType(p_this_MP4_Handler, id);
        if (MP4_IS_AUDIO_TRACK_TYPE(trackType))
        {
			uint8_t type = MP4GetTrackAudioMpeg4Type(p_this_MP4_Handler, id);
	        if ((type == MP4_MPEG4_AAC_LC_AUDIO_TYPE) ||
            	(type == MP4_MPEG4_AAC_SSR_AUDIO_TYPE) ||
            	(type == MP4_MPEG4_AAC_HE_AUDIO_TYPE)) {
				printf("TYPE %d\n", type);
            	return id;
        	}
		}
    }
    return 0;
}

void mm_mint_H264_Log_Callback( void *ctx, int level, const char* msg ) {
    printf("H264 trace %d %s\n", level, msg);
}

void mm_mint_mp4_Open(){

	mm_mint_mp4_vid.trace_level = WELS_LOG_INFO;

	mm_mint_mp4_snd.pMP4_Handler = (MP4FileHandle*)MP4Read( mm_mint_mp4_snd.filename );
	mm_mint_mp4_vid.pMP4_Handler = mm_mint_mp4_snd.pMP4_Handler;
	MP4FileHandle* p_mp4_handle = (MP4FileHandle*)mm_mint_mp4_snd.pMP4_Handler;

	mm_mint_mp4_vid.track_number = mm_mint_mp4_Get_First_VideoTrack( p_mp4_handle );
	mm_mint_mp4_snd.track_number = mm_mint_mp4_Get_First_AudioTrack( p_mp4_handle );

	if(mm_mint_mp4_snd.track_number > 0) {
		mm_mint_mp4_snd.is_playing = true;

		printf("***\tAudio Track ID %d\t***\n", mm_mint_mp4_snd.track_number);

		mm_mint_mp4_snd.pSndCodec_Handler = (NeAACDecHandle *)st_mem_alloc(sizeof(NeAACDecHandle));
		NeAACDecHandle*	p_snd_handle = (NeAACDecHandle *)mm_mint_mp4_snd.pSndCodec_Handler;
		*p_snd_handle = faacDecOpen();



		mm_mint_mp4_snd.total_frames = MP4GetTrackNumberOfSamples(p_mp4_handle, mm_mint_mp4_snd.track_number);
		mm_mint_mp4_snd.track_max_frame_size = MP4GetTrackMaxSampleSize(p_mp4_handle, mm_mint_mp4_snd.track_number);
		mm_mint_mp4_snd.track_sampleRate = MP4GetTrackTimeScale(p_mp4_handle, mm_mint_mp4_snd.track_number);
		mm_mint_mp4_snd.track_total_duration = double(MP4ConvertFromTrackDuration(p_mp4_handle, mm_mint_mp4_snd.track_number, MP4GetTrackDuration(p_mp4_handle, mm_mint_mp4_snd.track_number), MP4_MSECS_TIME_SCALE));
		mm_mint_mp4_snd.track_duration = MP4GetDuration(p_mp4_handle);

		if(mm_mint_mp4_snd.total_frames == 0){
			form_alert(1, "[1][ kbps seems to be equal to zero | Provide a valid m4p file ][Okay]");
			mm_mint_mp4_snd.track_number = 0;
			app_end = true;
			return;
		}

		mm_mint_mp4_snd.clk_prescale = (((25175000 / 256 ) / mm_mint_mp4_snd.track_sampleRate) - 1) ;
		switch (mm_mint_mp4_snd.clk_prescale) {
		case 1:
			mm_mint_mp4_snd.atari_effective_sampleRate = 49170;
			// mm_mint_mp4_snd.atari_effective_sampleRate = 49165;
			break;
		case 2:
			mm_mint_mp4_snd.atari_effective_sampleRate = 32780;
			break;
		case 3:
			mm_mint_mp4_snd.atari_effective_sampleRate = 24585;
			// mm_mint_mp4_snd.atari_effective_sampleRate = 24594;
			break;
		case 4:
			mm_mint_mp4_snd.atari_effective_sampleRate = 19668;
			break;	
		default:
			mm_mint_mp4_snd.atari_effective_sampleRate = mm_mint_mp4_snd.track_sampleRate;
			break;
		}

		switch (mm_mint_mp4_snd.track_sampleRate)
		{
		case 24000:
			mm_mint_mp4_snd.real_time_resampling = false;
			break;
		case 25050:
			mm_mint_mp4_snd.real_time_resampling = false;
			break;
		case 48000:
			mm_mint_mp4_snd.real_time_resampling = false;
			break;
		case 50000:
			mm_mint_mp4_snd.real_time_resampling = false;
			break;
		default:
			/* Down/Up Sampling */
			mm_mint_mp4_snd.real_time_resampling = false;
			if (mm_mint_mp4_snd.resampling_48kh == true && mm_mint_mp4_snd.track_sampleRate != 25050 ) {
				mm_mint_mp4_snd.new_samplerate = 48000;
				if( mm_mint_mp4_snd.new_samplerate != mm_mint_mp4_snd.track_sampleRate ){
					mm_mint_mp4_snd.clk_prescale = 1;
					mm_mint_mp4_snd.atari_effective_sampleRate = 49170;
					// mm_mint_mp4_snd.atari_effective_sampleRate = 49165;
					mm_mint_mp4_snd.ori_samplerate = mm_mint_mp4_snd.track_sampleRate;
					mm_mint_mp4_snd.track_sampleRate = mm_mint_mp4_snd.new_samplerate;
					FILTSIZE = 16;
					mm_mint_mp4_snd.real_time_resampling = true;
				} else {
					mm_mint_mp4_snd.real_time_resampling = false;
				}
			}
			if ( mm_mint_mp4_snd.resampling_25kh == true && mm_mint_mp4_snd.track_sampleRate != 48000 ) {
				mm_mint_mp4_snd.new_samplerate = 25050;
				if( mm_mint_mp4_snd.new_samplerate != mm_mint_mp4_snd.track_sampleRate ){
					mm_mint_mp4_snd.clk_prescale = 3;
					mm_mint_mp4_snd.atari_effective_sampleRate = 24585;
					// mm_mint_mp4_snd.atari_effective_sampleRate = 24594;
					mm_mint_mp4_snd.ori_samplerate = mm_mint_mp4_snd.track_sampleRate;
					mm_mint_mp4_snd.track_sampleRate = mm_mint_mp4_snd.new_samplerate;
					FILTSIZE = 16;
					mm_mint_mp4_snd.real_time_resampling = true;
				} else {
					mm_mint_mp4_snd.real_time_resampling = false;
				}
			}
			break;
		}

		config = faacDecGetCurrentConfiguration(*p_snd_handle);
		config->defObjectType = LC;
		config->outputFormat = FAAD_FMT_16BIT;
		faacDecSetConfiguration(*p_snd_handle, config);

		unsigned char *escfg;
		unsigned int escfglen;
		unsigned long this_samplerate;
		unsigned char this_channels;

		MP4GetTrackESConfiguration(p_mp4_handle, mm_mint_mp4_snd.track_number, &escfg, &escfglen);
		if (!escfg) {
			printf("---\tNo audio format information found\n");
		}
		if (faacDecInit(*p_snd_handle, escfg, escfglen, &this_samplerate, &this_channels) < 0) {
			printf("---\tCould not initialise FAAD\n");
		}
		printf("***\tFaad samplerate %lu\n", this_samplerate);
		printf("***\tFaad channels %d\n", this_channels);

		if(mm_mint_mp4_snd.real_time_resampling == true) {
			mm_mint_mp4_snd.resampling_ratio = (double)mm_mint_mp4_snd.new_samplerate / mm_mint_mp4_snd.ori_samplerate;
			VR = (VResampler *)Mxalloc(sizeof(VResampler), 0);
			if (VR->setup(mm_mint_mp4_snd.resampling_ratio, (unsigned int)this_channels, (unsigned int)FILTSIZE)) {
				fprintf (stderr, "Sample rate ratio %d/%d is not supported.\n", mm_mint_mp4_snd.new_samplerate, mm_mint_mp4_snd.ori_samplerate);
			} else {
				printf("***\tResample ratio %f\n", mm_mint_mp4_snd.resampling_ratio);
				printf("***\tOriginal sound samplerate %d -> Destination sound samplerate %d\t***\n***\tFilter HLEN used %d\t***\n", mm_mint_mp4_snd.ori_samplerate, mm_mint_mp4_snd.new_samplerate, FILTSIZE);
			}
		}

		mm_mint_mp4_snd.track_channels = this_channels;

		mm_mint_mp4_snd.bytes_counter = 0;
		mm_mint_mp4_snd.frames_counter = 1;
	}

	if(mm_mint_mp4_vid.track_number > 0) {
		mm_mint_mp4_vid.is_playing = true;
		printf("***\tVideo Track ID %d\t***\n", mm_mint_mp4_vid.track_number);
		mm_mint_mp4_vid.trace_level = WELS_LOG_ERROR;
		// mm_mint_mp4_vid.trace_level = WELS_LOG_INFO;

		if (WelsCreateDecoder(&pVidCodec_Handler)) { printf("Create Decoder failed\n"); }
		if (!pVidCodec_Handler) { printf("Create Decoder failed (no handle)\n"); }
		WelsTraceCallback call_back_trace_function = mm_mint_H264_Log_Callback;
		if(pVidCodec_Handler->SetOption(DECODER_OPTION_TRACE_CALLBACK, (void*)&call_back_trace_function)){ printf("SetOption failed\n"); }
		if(pVidCodec_Handler->SetOption(DECODER_OPTION_TRACE_LEVEL, &mm_mint_mp4_vid.trace_level)){ printf("SetOption failed\n"); }
		pVidCodec_Param.uiTargetDqLayer = UCHAR_MAX;
		pVidCodec_Param.bParseOnly = false;
		pVidCodec_Param.eEcActiveIdc = ERROR_CON_SLICE_COPY;
		pVidCodec_Param.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_AVC;
		if(pVidCodec_Handler->Initialize (&pVidCodec_Param)) { printf("initialize failed\n"); }
		pDstBufInfo = 	(SBufferInfo*)st_mem_alloc(sizeof(SBufferInfo));
		memset(pDstBufInfo, 0, sizeof(SBufferInfo));

		mm_mint_mp4_vid.track_total_frames = MP4GetTrackNumberOfSamples(p_mp4_handle, mm_mint_mp4_vid.track_number);
		mm_mint_mp4_vid.track_max_frame_size = MP4GetTrackMaxSampleSize(p_mp4_handle, mm_mint_mp4_vid.track_number);
		mm_mint_mp4_vid.track_width = MP4GetTrackVideoWidth(p_mp4_handle, mm_mint_mp4_vid.track_number);
		mm_mint_mp4_vid.track_height = MP4GetTrackVideoHeight(p_mp4_handle, mm_mint_mp4_vid.track_number);
		mm_mint_mp4_vid.track_fps = MP4GetTrackVideoFrameRate(p_mp4_handle, mm_mint_mp4_vid.track_number);
		mm_mint_mp4_vid.video_ratio = (double)mm_mint_mp4_vid.track_width / mm_mint_mp4_vid.track_height;
		mm_mint_mp4_vid.track_media_data_name = MP4GetTrackMediaDataName(p_mp4_handle, mm_mint_mp4_vid.track_number);
		mm_mint_mp4_vid.time_per_frame = ((double)1000 / mm_mint_mp4_vid.track_fps);
		mm_mint_mp4_vid.track_total_duration = double(MP4ConvertFromTrackDuration(p_mp4_handle, mm_mint_mp4_vid.track_number, MP4GetTrackDuration(p_mp4_handle, mm_mint_mp4_vid.track_number), MP4_MSECS_TIME_SCALE));
		
		uint8_t **pSeqHeaders, **pPictHeaders, *video_sample;
		uint32_t *pSeqHeaderSize, *pPictHeaderSize, video_sample_offset = 0;

		MP4GetTrackH264SeqPictHeaders(p_mp4_handle, mm_mint_mp4_vid.track_number, &pSeqHeaders, &pSeqHeaderSize, &pPictHeaders, &pPictHeaderSize);

		// if no audio mp4v2 seems to crash here
		mm_mint_mp4_vid.video_sample_max_size = MP4GetTrackMaxSampleSize(p_mp4_handle, mm_mint_mp4_vid.track_number) << 1;

		mm_mint_mp4_vid.video_sample = (uint8_t*)st_mem_alloc(mm_mint_mp4_vid.video_sample_max_size);

		video_sample = mm_mint_mp4_vid.video_sample;
			if (pSeqHeaders && pSeqHeaderSize) {
				for(int i = 0; (pSeqHeaders[i] && pSeqHeaderSize[i]); i++) {
					(*(uint32_t *)(video_sample + video_sample_offset)) = 0X00000001;
					video_sample_offset += 4;
					memcpy(video_sample + video_sample_offset, pSeqHeaders[i], pSeqHeaderSize[i]);
					video_sample_offset += pSeqHeaderSize[i];
				}
			}
			if (pPictHeaders && pPictHeaderSize) {
				for(int i = 0; (pPictHeaders[i] && pPictHeaderSize[i]); i++) {
					(*(uint32_t *)(video_sample + video_sample_offset)) = 0X00000001;
					video_sample_offset += 4;
					memcpy(video_sample + video_sample_offset, pPictHeaders[i], pPictHeaderSize[i]);
					video_sample_offset += pPictHeaderSize[i];
				}
			}
		mm_mint_mp4_vid.video_sample_offset = video_sample_offset;

		if(mm_mint_mp4_snd.track_number > 0){
			mm_mint_mp4_vid.computed_track_fps = ((double)mm_mint_mp4_snd.atari_effective_sampleRate * mm_mint_mp4_vid.track_fps) / (double)mm_mint_mp4_snd.track_sampleRate;
			mm_mint_mp4_vid.time_per_frame = (double)mm_mint_mp4_snd.atari_effective_sampleRate / (double)mm_mint_mp4_vid.computed_track_fps;
			printf("Original Samplerate %d\nEffective samplerate %d\n", mm_mint_mp4_snd.track_sampleRate, mm_mint_mp4_snd.atari_effective_sampleRate);
			printf("Original FPS %.1ff/s\nEffective FPS %.1ff/s\n", mm_mint_mp4_vid.track_fps, mm_mint_mp4_vid.computed_track_fps);
			printf("Time per frame computed : 1 every %03fHz\n", mm_mint_mp4_vid.time_per_frame);
		} else {
			mm_mint_mp4_vid.computed_track_fps = mm_mint_mp4_vid.track_fps;
			mm_mint_mp4_vid.time_per_frame = mm_mint_mp4_vid.time_per_frame / (double)clock_unit;
		}
		mm_mint_mp4_vid.computed_total_time_sec = mm_mint_mp4_vid.track_total_frames / mm_mint_mp4_vid.computed_track_fps; 

		mm_mint_mp4_vid.frames_counter = 1;
		mm_mint_mp4_vid.frames_dropped = 0;
	}

	if(mm_mint_mp4_vid.track_number > 0) {
		if( mm_mint_mp4_vid.track_width < (wdesk - 20) && mm_mint_mp4_vid.track_height < (hdesk - 80) ){
			open_window(mm_mint_mp4_vid.track_width, mm_mint_mp4_vid.track_height, basename(mm_mint_mp4_snd.filename) );
			mm_ico_Update_x(2, wwork - mm_ico_padding_right, -1);
		} else {
			/* 240p default = 320×240 */
			open_window( VIDEO_WIDTH, VIDEO_HEIGHT, basename(mm_mint_mp4_snd.filename) );	
			mm_ico_Update_x(2, wwork - mm_ico_padding_right, -1);
		}
	} else {
		int16_t width = 320;
		int16_t height = 50;
		mm_ico_win_delta_y = 40;
		open_window( width, height, basename(mm_mint_mp4_snd.filename) );
		uint32_t st_modified_buffer_size = (MFDB_STRIDE(width) * height) * (work_out_extended[4] >> 3);
		int8_t *st_modified_buffer = (int8_t *)st_mem_alloc(st_modified_buffer_size);
		memset( st_modified_buffer, 0, st_modified_buffer_size );
		mm_wi_mfdb.fd_nplanes = work_out_extended[4];
		mm_wi_mfdb.fd_stand = 0;
		mm_wi_mfdb.fd_w = MFDB_STRIDE(width);
		mm_wi_mfdb.fd_h = height;
		mm_wi_mfdb.fd_wdwidth = MFDB_STRIDE(mm_wi_mfdb.fd_w) >> 4;
		mm_wi_mfdb.fd_addr = st_modified_buffer;

		int8_t* mm_control_bar_buffer = &st_modified_buffer[ (MFDB_STRIDE(width) * (height - mm_ico_win_delta_y)) * (work_out_extended[4] >> 3) ];
		mm_control_bar_mfdb.fd_addr = mm_control_bar_buffer;
		mm_control_bar_mfdb.fd_h = mm_ico_win_delta_y;
		mm_control_bar_mfdb.fd_w = MFDB_STRIDE(width);
		mm_control_bar_mfdb.fd_stand = 0;
		mm_control_bar_mfdb.fd_wdwidth = MFDB_STRIDE( mm_control_bar_mfdb.fd_w ) >> 4;
		mm_control_bar_mfdb.fd_nplanes = work_out_extended[4];
		mm_ico_Update_x(2, wwork - mm_ico_padding_right, -1);
		mm_ico_Handle(0, 0, 0);
	}
	st_Send_WM_REDRAW();
}

void mm_mint_mp4_Snd_MP4_Decode( int8_t *pBuffer, uint32_t bufferSize ){

    int16_t		frame_size = 0;

    uint32_t	frame_counter = 0, done = 0;
    INT_PCM*	pDec_data = NULL;

	NeAACDecHandle*  	p_snd_handle = (NeAACDecHandle*)mm_mint_mp4_snd.pSndCodec_Handler;
    MP4FileHandle*      p_mp4_handle = (MP4FileHandle*)mm_mint_mp4_snd.pMP4_Handler;

	if(pData == NULL){
		pData = (uint8_t*)st_mem_alloc(mm_mint_mp4_snd.track_max_frame_size);
	}


	if(mm_mint_mp4_snd.is_paused != true){
		while( done < bufferSize && mm_mint_mp4_snd.frames_counter < mm_mint_mp4_snd.total_frames && app_end != true) {

			uint32_t packet_size = mm_mint_mp4_snd.track_max_frame_size;

			MP4ReadSample(p_mp4_handle, mm_mint_mp4_snd.track_number, mm_mint_mp4_snd.frames_counter, (uint8_t **) &pData, &packet_size, NULL, NULL, NULL, NULL);

			NeAACDecFrameInfo snd_info;
			pDec_data = (INT_PCM*)NeAACDecDecode(*p_snd_handle, &snd_info, pData, packet_size );
			if(snd_info.error > 0){
				printf("[FAAD] Error decoding %s\n", faacDecGetErrorMessage(snd_info.error) );
			}
			if (!snd_info.samples || !pDec_data || !snd_info.bytesconsumed) {
				printf("[FAAD] empty/non complete samples #%lu bytesconsumed #%lu\n", snd_info.samples, snd_info.bytesconsumed);
			}

			frame_size = snd_info.samples;

			if( mm_mint_mp4_snd.real_time_resampling == true ){

				float	downscale_buff_in[frame_size];
				float	downscale_buff_out[frame_size * (uint32_t)mm_mint_mp4_snd.resampling_ratio];
				INT_PCM	downscale_buff_pcm[frame_size * (uint32_t)mm_mint_mp4_snd.resampling_ratio];

				uint32_t	snd_in_size = snd_info.samples >> 1;
				uint32_t	snd_out_size = lrint((snd_in_size >> 1) * mm_mint_mp4_snd.resampling_ratio) << 1;

				mm_mint_mp4_Snd_PCM16_to_Float(downscale_buff_in, (INT_PCM*)pDec_data, frame_size);

				VR->inp_data = downscale_buff_in;
				VR->out_data = downscale_buff_out;
				VR->inp_count = snd_in_size;
				VR->out_count = snd_out_size;

				VR->process();

				frame_size = snd_out_size << 1;
				if(mm_mint_mp4_snd.is_muted != true){
					int16_t* tmp_ptr = (int16_t*)&pBuffer[done];
					mm_mint_mp4_Snd_Float_to_PCM16(tmp_ptr, downscale_buff_out, frame_size);
				} else {
					memset( &pBuffer[done], 0, frame_size * sizeof(INT_PCM) );
				}
			} else {
				if(mm_mint_mp4_snd.is_muted != true){
					u_int16_t* this_ptr = (u_int16_t*)&pBuffer[done];
					for (int16_t i = 0; i < frame_size; i++) {
						*this_ptr++ = (( ((INT_PCM*)pDec_data)[i] ) &0xFF00) | ((INT_PCM*)pDec_data)[i] & 0x00FF;
					}					
				} else {
					memset( &pBuffer[done], 0, frame_size * sizeof(INT_PCM) );
				}
			}

			done += frame_size * sizeof(INT_PCM);
			frame_counter += 1;
			mm_mint_mp4_snd.frames_counter++;

		}
	} else {
		memset( pBuffer, 0, bufferSize);
	}
    if( pDec_data != NULL){
		free(pDec_data);
		pDec_data = NULL;
	}
    mm_mint_mp4_snd.bytes_counter += done;
}

void mm_mint_mp4_Vid_MP4_Decode(){

    MP4FileHandle	*p_mp4_handle = (MP4FileHandle*)mm_mint_mp4_snd.pMP4_Handler;

	MP4Duration mp4_duration;
	uint32_t sample_size, video_sample_offset;
	uint8_t *video_sample_start_addr, *video_sample, *tmp_addr;

	uint32_t width, height, original_width, original_height;
	uint32_t out_size, max_out_size, st_modified_buffer_size, pDestData_size;
	uint32_t scaled_half_size;

    uint8_t *pData[3] = {0};
    uint8_t *pDestData = NULL;
    uint8_t *pDestY, *pDestU, *pDestV;

	int16_t  nb_components_32bits = 4;

	int8_t *st_modified_buffer = NULL;

	int16_t win_xy[8];
	int16_t rect_pxy[8];
	int16_t xy_clip[4];
	GRECT rect;

	uint32_t snd_bytes_processed, frame_to_play, previous_frame;
	bool skip_frame = false;

	uint32_t	previous_snd_hz_processed = 0;
	uint32_t	snd_hz_processed = 0;
	float		real_time_fps = 0;
	char		fps_indicator[5];
	char		time_left_sec[5];

	int8_t *mm_control_bar_buffer;

	uint32_t fps_max_delta = (uint32_t)mm_mint_mp4_vid.computed_track_fps >> 2;

	time_start = st_Supexec(get200hz);

	if(mm_mint_mp4_snd.track_number > 0) {
		time_start_witness = mm_mint_mp4_Snd_Get_Playback_Position();
	}
	else {
		time_start_witness = st_Supexec(get200hz);
	}

	while( mm_mint_mp4_vid.frames_counter < mm_mint_mp4_vid.track_total_frames && app_end != true) {
		if(MP4GetSampleSync(p_mp4_handle, mm_mint_mp4_vid.track_number, mm_mint_mp4_vid.frames_counter)) {
			video_sample = mm_mint_mp4_vid.video_sample;
			video_sample_offset = mm_mint_mp4_vid.video_sample_offset;
		} else {
			video_sample = mm_mint_mp4_vid.video_sample + mm_mint_mp4_vid.video_sample_offset;
			video_sample_offset = 0;
		}
		mp4_duration = 0;
		sample_size = mm_mint_mp4_vid.video_sample_max_size - video_sample_offset;
		video_sample_start_addr = video_sample + video_sample_offset;

		MP4ReadSample(p_mp4_handle, mm_mint_mp4_vid.track_number, mm_mint_mp4_vid.frames_counter, &video_sample_start_addr, &sample_size, NULL, &mp4_duration, NULL, NULL);

		tmp_addr = video_sample_start_addr;
		while((tmp_addr - video_sample_start_addr) < sample_size)
		{
			uint32_t *p = (uint32_t *) tmp_addr;
			uint32_t header_size = *p;
			*p = 0X00000001;
			tmp_addr += (header_size + 4);
		}

		sample_size += video_sample_offset;
		pVidCodec_Handler->DecodeFrameNoDelay( video_sample, sample_size, pData, pDstBufInfo );
		if( pDstBufInfo->iBufferStatus == 1 && skip_frame == false) {		
			original_width = pDstBufInfo->UsrData.sSystemBuffer.iWidth;
			original_height = pDstBufInfo->UsrData.sSystemBuffer.iHeight;
			mm_mint_mp4_vid.video_ratio = (float)original_width / original_height;
			int Y_Stride = pDstBufInfo->UsrData.sSystemBuffer.iStride[0];
			int UV_Stride = pDstBufInfo->UsrData.sSystemBuffer.iStride[1];

			if(mm_mint_mp4_vid.limit_upscale == true){
				width = MIN(original_width, wwork);
				height = MIN(original_height, hwork);
				max_out_size = original_width * original_height;
			} else {
				width = wwork;
				height = hwork;
			}

			out_size = width * height;
			scaled_half_size = (width + 1) >> 1;

			if(mm_mint_mp4_vid.limit_upscale == true){
				st_modified_buffer_size = (MFDB_STRIDE(original_width) * original_height) * ((work_out_extended[4]) >> 3);
				pDestData_size = ( max_out_size << 1 );
			} else {
				st_modified_buffer_size = (MFDB_STRIDE(width) * (height + 1)) * ((work_out_extended[4]) >> 3);
				pDestData_size = ( out_size << 1 );
				mm_wi_mfdb.fd_addr = NULL;
				if(mm_mint_mp4_vid.frames_counter > 0){
					st_mem_free( st_modified_buffer );
					st_mem_free( pDestData );
				}
				st_modified_buffer = NULL;
				pDestData = NULL;
			}
			if (st_modified_buffer == NULL) {
				st_modified_buffer = (int8_t *)st_mem_alloc(st_modified_buffer_size);
			}
			if( pDestData == NULL ) { 
				pDestData = (uint8_t*)st_mem_alloc(pDestData_size);
			}
			memset(pDestData, 0, pDestData_size);
			memset(st_modified_buffer, 0, st_modified_buffer_size);
			if( mm_mint_mp4_vid.limit_upscale == true && (wwork >= mm_mint_mp4_vid.track_width || hwork >= mm_mint_mp4_vid.track_height) )
			{
				switch (work_out_extended[4]){
					case 32:
						libyuv::I420ToBGRA( 	pData[0], Y_Stride,
												pData[1], UV_Stride,
												pData[2], UV_Stride, 
												(uint8_t*)st_modified_buffer, 
												MFDB_STRIDE(width) << 2, 
												width, height);
						mm_wi_mfdb.fd_nplanes = 32;
					break;
					case 24:
						libyuv::I420ToRGB24( 	pData[0], Y_Stride,
									pData[1], UV_Stride,
									pData[2], UV_Stride, 
									(uint8_t*)st_modified_buffer, 
									MFDB_STRIDE(width) * 3, 
									width, height);
						mm_wi_mfdb.fd_nplanes = 24;
						// for( u_int16_t y = 0; y < height; y++ ) {
						// 	for( u_int16_t x = 0; x < width; x++ ) {
						// 		uint32_t index = ((y * MFDB_STRIDE(width)) + x) * 3;
						// 		uint8_t b = *st_modified_buffer++;
						// 		uint8_t g = *st_modified_buffer++;
						// 		uint8_t r = *st_modified_buffer++;
						// 		st_modified_buffer[ index++ ] = r;
						// 		st_modified_buffer[ index++ ] = g;
						// 		st_modified_buffer[ index++ ] = b;
						// 	}
						// }						
					break;
					case 16:
						libyuv::I420ToRGB565( 	pData[0], Y_Stride,
									pData[1], UV_Stride,
									pData[2], UV_Stride, 
									(uint8_t*)st_modified_buffer, 
									MFDB_STRIDE(width) << 1, 
									width, height);
						mm_wi_mfdb.fd_nplanes = 16;
					break;
					default:
						form_alert(1, "[1][ Pixels alignment not supported ][Okay]");
						return;					
					break;
				}
			}
			else
			{
				pDestY = pDestData;
				pDestU = pDestData + out_size;
				pDestV = pDestU + (scaled_half_size * scaled_half_size);

				libyuv::I420Scale(      pData[0], Y_Stride,
										pData[1], UV_Stride,
										pData[2], UV_Stride,
										original_width, original_height, 
										pDestY, width, 
										pDestU, scaled_half_size, 
										pDestV, scaled_half_size, 
										width, height, 
										(libyuv::FilterMode)0 );
				switch (work_out_extended[4]){
					case 32:
						libyuv::I420ToBGRA( 	pDestY, width,
												pDestU, scaled_half_size,
												pDestV, scaled_half_size, 
												(uint8_t*)st_modified_buffer, 
												MFDB_STRIDE(width) << 2, 
												width, height);
						mm_wi_mfdb.fd_nplanes = 32;
					break;
					case 24:
						libyuv::I420ToRGB24( 	pDestY, width,
												pDestU, scaled_half_size,
												pDestV, scaled_half_size, 
												(uint8_t*)st_modified_buffer, 
												MFDB_STRIDE(width) * 3, 
												width, height);
						mm_wi_mfdb.fd_nplanes = 24;
					break;					
					case 16:
						libyuv::I420ToRGB565( 	pDestY, width,
									pDestU, scaled_half_size,
									pDestV, scaled_half_size, 
									(uint8_t*)st_modified_buffer, 
									MFDB_STRIDE(width) << 1, 
									width, height);
						mm_wi_mfdb.fd_nplanes = 16;
					break;
					default:
					break;
				}
			}

			mm_wi_mfdb.fd_stand = 0;
			mm_wi_mfdb.fd_w = MFDB_STRIDE(width);
			mm_wi_mfdb.fd_h = height;
			mm_wi_mfdb.fd_wdwidth = MFDB_STRIDE(mm_wi_mfdb.fd_w) >> 4;
			mm_wi_mfdb.fd_addr = st_modified_buffer;

			if(mm_mint_mp4_snd.track_number > 0) {
				snd_bytes_processed = mm_mint_mp4_snd.bytes_counter > (mm_mint_mp4_snd.bufferSize << 1) ? mm_mint_mp4_snd.bytes_counter - (mm_mint_mp4_snd.bufferSize << 1) : 0;
				frame_to_play = (( ( snd_bytes_processed >> 2 ) + mm_mint_mp4_Snd_Get_Playback_Position() ) / mm_mint_mp4_vid.time_per_frame) + 1;
			}

			if(mm_mint_mp4_snd.track_number > 0) {
				time_end_witness = mm_mint_mp4_Snd_Get_Playback_Position();
				time_end_witness = time_end_witness < time_start_witness ? time_end_witness + mm_mint_mp4_snd.atari_effective_sampleRate : time_end_witness;
			} else {
				time_end_witness = st_Supexec(get200hz);
			}

			if(mm_mint_mp4_snd.track_number > 0) {
				if(frame_to_play < mm_mint_mp4_vid.frames_counter - 1){
					while ( (time_end_witness - time_start_witness) < mm_mint_mp4_vid.time_per_frame ) {
						time_end_witness = mm_mint_mp4_Snd_Get_Playback_Position();
						time_end_witness = time_end_witness < time_start_witness ? time_end_witness + mm_mint_mp4_snd.atari_effective_sampleRate : time_end_witness;
					}
				}
			}
			else {
				while ( (time_end_witness - time_start_witness) < mm_mint_mp4_vid.time_per_frame ) {
					time_end_witness = st_Supexec(get200hz);
				}
			}

			if(mm_mint_mp4_snd.track_number > 0) {
				if(frame_to_play > mm_mint_mp4_vid.frames_counter + fps_max_delta){
					/* This printf the desync with sound playing */
					// printf("Frame to play %lu vs Frame playing %lu\n",frame_to_play, mm_mint_mp4_vid.frames_counter);
					bool is_sync_frame = false;
					while(!is_sync_frame && frame_to_play > mm_mint_mp4_vid.frames_counter + 5){
						if(MP4GetSampleSync(p_mp4_handle, mm_mint_mp4_vid.track_number, mm_mint_mp4_vid.frames_counter + 1)){
							is_sync_frame = true;
						}
						mm_mint_mp4_vid.frames_counter += 1;
						mm_mint_mp4_vid.frames_dropped += 1;
					}
					mm_mint_mp4_vid.frames_counter -= 1;
					time_end_witness = mm_mint_mp4_Snd_Get_Playback_Position();
				}
			}
			
			if(mm_mint_mp4_snd.track_number > 0) {
				real_time_fps = mm_mint_mp4_snd.atari_effective_sampleRate / (time_end_witness - time_start_witness);
				time_start_witness = mm_mint_mp4_Snd_Get_Playback_Position();
			}
			else {
				real_time_fps = 1000 / ((time_end_witness - time_start_witness) * clock_unit);
				time_start_witness = st_Supexec(get200hz);
			}

			mm_mint_mp4_vid.computed_current_time_sec = MIN(mm_mint_mp4_vid.frames_counter /  mm_mint_mp4_vid.computed_track_fps, mm_mint_mp4_vid.computed_total_time_sec);
			mm_mint_mp4_vid.computed_left_time_sec = mm_mint_mp4_vid.computed_total_time_sec - mm_mint_mp4_vid.computed_current_time_sec;

			sprintf(fps_indicator, "%.1f", real_time_fps);
			sprintf(time_left_sec, "%lu secs", mm_mint_mp4_vid.computed_left_time_sec);

			if(mm_ico_win_delta_y > 0){
				mm_control_bar_buffer = &st_modified_buffer[ (MFDB_STRIDE(width) * (height - mm_ico_win_delta_y)) * ((work_out_extended[4]) >> 3) ];
				mm_control_bar_mfdb.fd_addr = mm_control_bar_buffer;
				mm_control_bar_mfdb.fd_h = mm_ico_win_delta_y;
				mm_control_bar_mfdb.fd_w = MFDB_STRIDE(width);
				mm_control_bar_mfdb.fd_stand = 0;
				mm_control_bar_mfdb.fd_wdwidth = MFDB_STRIDE( mm_control_bar_mfdb.fd_w ) >> 4;
				mm_control_bar_mfdb.fd_nplanes = work_out_extended[4];
				mm_ico_Handle(0, 0, 0);
			}
			

			/* Source MFDB buffer */
			win_xy[0] = 0; win_xy[1] = 0; win_xy[2] = mm_wi_mfdb.fd_w - 1; win_xy[3] = mm_wi_mfdb.fd_h - 1;
			/* Destination MFDB Buffer */
			win_xy[4] = xwork; win_xy[5] = ywork; win_xy[6] = xwork + mm_wi_mfdb.fd_w - 1; win_xy[7] = ywork + mm_wi_mfdb.fd_h - 1;
			hide_mouse();
			wind_update(BEG_UPDATE);
			wind_get(wi_handle, WF_FIRSTXYWH, &rect.g_x, &rect.g_y, &rect.g_w, &rect.g_h);
			while(rect.g_h  != 0 && rect.g_w != 0){
				if ( rc_intersect((GRECT*)&win_xy[4], &rect) ){
					set_clip(1, rect.g_x, rect.g_y, rect.g_w, rect.g_h);
					vro_cpyfm(handle, S_ONLY, win_xy, &mm_wi_mfdb, &screen_mfdb);
				}
				wind_get( wi_handle ,WF_NEXTXYWH ,&rect.g_x , &rect.g_y, &rect.g_w, &rect.g_h );
			}
			wind_update(END_UPDATE);
			show_mouse();
			vswr_mode( handle, MD_TRANS);			
			v_gtext(handle, xwork + mm_wi_mfdb.fd_w - (gl_wbox << 2) , ywork + gl_hbox,  fps_indicator);
			v_gtext(handle, xwork + (gl_wbox << 2) , ywork + gl_hbox,  time_left_sec);
			vswr_mode( handle, MD_REPLACE);
			if(mm_ico_win_delta_y > 0){
				st_mem_free(mm_control_bar_buffer);
			}
		}
		while(mm_mint_mp4_vid.is_paused == true && app_end != true){
			pthread_yield_np();
		}
		mm_mint_mp4_vid.frames_counter += 1;
		pthread_yield_np();
		
	}
	time_end = st_Supexec(get200hz);
	duration = clock_unit * (time_end - time_start);
}

void mm_mint_mp4_Close(){
	MP4FileHandle*      p_mp4_handle = (MP4FileHandle*)mm_mint_mp4_snd.pMP4_Handler;
	if(p_mp4_handle != NULL){
		MP4Close(p_mp4_handle);
		st_mem_free(p_mp4_handle);	
	}
}

void mm_mint_mp4_Snd_Close(){
	if(mm_mint_mp4_snd.track_number > 0) {

		NeAACDecHandle*  p_snd_handle = (NeAACDecHandle*)mm_mint_mp4_snd.pSndCodec_Handler;
		faacDecClose(*p_snd_handle);
		st_mem_free(pData);
		mm_mint_mp4_snd.bytes_counter = 0;
		if(mm_mint_mp4_snd.real_time_resampling == true){
			VR->clear();
			Mfree(VR);
		}

		mm_mint_mp4_Snd_mem_free(mm_mint_mp4_snd.pBuffer);
	}
}

void mm_mint_mp4_Vid_Close(){

	if(mm_mint_mp4_vid.track_number > 0) {
		time_end = st_Supexec(get200hz);
		duration = clock_unit * (time_end - time_start);
		printf("\n**\tTotal frames %d\t **\n**\tTotal frames dropped %d\t **\n**\tduration %d ms\t**\n**\tFPS = %lu\t**\n", 
				mm_mint_mp4_vid.frames_counter, 
				mm_mint_mp4_vid.frames_dropped,
				duration, 
				( (mm_mint_mp4_vid.frames_counter - mm_mint_mp4_vid.frames_dropped) / (duration / 1000)) );
		if(pVidCodec_Handler != NULL){

			pVidCodec_Handler->Uninitialize();
			WelsDestroyDecoder(pVidCodec_Handler);
			st_mem_free(pDstBufInfo);

		}
		st_mem_free(mm_wi_mfdb.fd_addr);
	}

}

void* mm_mint_mp4_Vid_Play(void* p_param){
	if(mm_mint_mp4_vid.track_number > 0){
		while( mm_mint_mp4_vid.frames_counter < mm_mint_mp4_vid.track_total_frames && app_end != true) {
			mm_mint_mp4_vid.mm_mint_mp4_Vid_Feed();
			pthread_yield_np();
		}
	}
	return NULL;
}

void* mm_mint_mp4_Snd_Play(void* p_param ){
	if(mm_mint_mp4_snd.track_number > 0){
		while( app_end != true){
			if(mm_mint_mp4_snd.frames_counter < mm_mint_mp4_snd.total_frames){
				mm_mint_mp4_Snd_Feed();
				pthread_yield_np();
			} else {
				/* This show how much frames was played before sound restart */
				// printf("mm_mint_mp4_snd.frames_counter %d - mm_mint_mp4_snd.total_frames %d\n",mm_mint_mp4_snd.frames_counter ,mm_mint_mp4_snd.total_frames);
				// mm_mint_mp4_Snd_Restart();
				mm_mint_mp4_Snd_Pause();
				pthread_yield_np();
			}
		}
	}
	return NULL;
}

/****************************************************************/
/*  MISC UTILITY ROUTINES                                       */
/****************************************************************/

bool check_ext(const char *ext1, const char *ext2){
	u_int16_t i;
	bool lower_detected;
	lower_detected = FALSE;

	for (i = 0; ext1[i] != '\0'; i++) {
		if (ext1[i] >= 'a' && ext1[i] <= 'z'){
			lower_detected = TRUE;
		break;
		}
	}
	if(lower_detected){
		stringtolower((char *)ext2);
	} else {
		stringtoupper((char *)ext2);
	}
	if(strcmp(ext1, ext2) == 0){
		return TRUE;
	} else {
		return FALSE;
	}
}

void stringtolower(char *ext){
	u_int8_t i;
	u_int8_t s_len;

	s_len = strlen(ext);
    for (i = 0; i < s_len; i++)
    { 
        ext[i] = tolower(ext[i]); 
    }
}

void stringtoupper(char *ext){
	u_int8_t i;
	u_int8_t s_len;

	s_len = strlen(ext);
    for (i = 0; i < s_len; i++)
    { 
        ext[i] = toupper(ext[i]); 
    }
}

const char *get_filename_ext(const char *filename){
    const char *dot = strrchr(filename, '.');
    if(!dot || dot == filename) return "";
    return dot + 1;
}

void remove_quotes(char* s1, char* s2){
    char *dst = s1;
    char *src = s2;
    char c;

    while ((c = *src++) != '\0')
    {
        if (c == '\\')
        {
            *dst++ = c;
            if ((c = *src++) == '\0')
                break;
            *dst++ = c;
        }
        else if (c != '"')
            *dst++ = c;
    }
    *dst = '\0';
}