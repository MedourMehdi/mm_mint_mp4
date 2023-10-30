#include <mint/osbind.h>
#include <mint/mintbind.h>
#include <string.h> /* memset */
#include "./mm_vscode_fix.h"

/*
	M.Medour 2022/09
	The purpose of redefining functions here is to avoid vscode error message like "nom de registre inconnu 'd0'"
	This is caused by trap definitions & vscode don't know motorola 68k registers.
*/

u_int16_t ARGB_to_RGB565(u_int8_t *ARGBPixel)
{
    u_int16_t b = (ARGBPixel[3] >> 3) & 0x1f;
    u_int16_t g = ((ARGBPixel[2] >> 2) & 0x3f) << 5;
    u_int16_t r = ((ARGBPixel[1] >> 3) & 0x1f) << 11;

    return (u_int16_t) (r | g | b);
}


void* st_mem_alloc(int32_t size){
	void* mem_ptr = NULL;
	mem_ptr = (void*)Mxalloc(size,3);
	return mem_ptr;
}

void *st_mem_free(void *ptr){
	if(ptr != NULL){
		Mfree(ptr);
		ptr = NULL;
	}
	return ptr;
}

void *st_mem_calloc(int32_t size){
	void *mem_ptr = NULL;
	mem_ptr = (void*)Mxalloc(size,3);
	memset(mem_ptr, 0, size);
	return mem_ptr;
}

int16_t st_Pgetpid(){
    return Pgetpid();
}

int16_t st_Syield(){
    return Syield();
}

int32_t st_Fselect( u_int16_t timeout, int32_t *rfds, int32_t *wfds){
    return Fselect( timeout, rfds, wfds, 0L );
}

int32_t st_Supexec(int32_t atari_hardware_addr()){
	return Supexec(atari_hardware_addr);
}

int32_t get200hz(void)
{
	return *((int32_t*)0x4ba);
}

void *mm_mint_mp4_Snd_mem_alloc(int32_t size){
	void *mem_ptr = NULL;
	mem_ptr = (void*)Mxalloc(size,0);
	// mem_ptr = (void*)malloc(size);
	return mem_ptr;
}

void *mm_mint_mp4_Snd_mem_free(void *ptr){
	Mfree(ptr);
	return NULL;
}

void enableTimerASei( void )
{
	*( (volatile unsigned char*)0xFFFFFA17L ) |= ( 1<<3 );	//	software end-of-interrupt mode
}

void st_enableTimerASei( void )
{
	Supexec(enableTimerASei);
}

void st_Locksnd(){
	Locksnd();
}

void st_Unlocksnd(){
	Unlocksnd();
}

int32_t st_Soundcmd(  int16_t mode, int16_t data  ){
	int32_t ret = Soundcmd( mode, data );
	return ret;
}

int32_t st_Sndstatus(  int16_t reset  ){
	int32_t ret = Sndstatus( reset );
	return ret;
}

int32_t st_Devconnect( int16_t src, int16_t dst, int16_t srcclk, int16_t prescale, int16_t protocol ){
	int32_t ret = Devconnect( src, dst, srcclk, prescale, protocol );
	return ret;
}

int32_t st_Setmontracks( int16_t montrack ){
	int32_t ret = Setmontracks( montrack );
	return ret;
}

int32_t st_Setmode(  int16_t mode  ){
	int32_t ret = Setmode( mode );
	return ret;
}

int32_t st_Settracks( int16_t playtracks, int16_t rectracks ){
	int32_t ret = Settracks(playtracks, rectracks);
	return ret;
}

int32_t st_Setbuffer( int16_t reg, void *begaddr, void *endaddr ){
	int32_t ret = Setbuffer( reg, begaddr, endaddr );
	return ret;
}

int32_t st_Setinterrupt( int16_t src_inter, int16_t cause ){
	int32_t ret = Setinterrupt( src_inter,cause );
	return ret;
}

void st_Xbtimer(int16_t timer, int16_t control, int16_t data, void(*vector)( )){
	Xbtimer(timer, control, data, vector);
}

void st_Jenabint( int16_t timer ){
	Jenabint( timer );
}
	

int32_t	st_Buffoper( int16_t mode ){
	int32_t ret = Buffoper( mode );
	return ret;
};

void st_Jdisint( int16_t timer ){
	Jdisint( timer );
}

int32_t st_Buffptr( int32_t *ptr ){
		int32_t ret = Buffptr( ptr );
		return ret;
}

void st_Vsync( int16_t nb_vsync ){
	for(int16_t i = 0; i < nb_vsync; i++){
		Vsync(); // 20ms
	}
}