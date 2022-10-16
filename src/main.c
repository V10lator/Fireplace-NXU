#include <coreinit/foreground.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/memory.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <gx2/event.h>
#include <proc_ui/procui.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_events.h>
#include <SDL2/SDL_mixer.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sysapp/launch.h>

#define WIDTH 640
#define HEIGHT 54
#define WIN_WIDTH 1280
#define WIN_HEIGHT 720
#define FPS 20

#define FS_ALIGN(x) ((x + 0x3F) & ~(0x3F))
#define COLOR(r,g,b) ((((uint32_t)((((r) * 4) << 16) | ((g) * 4 << 8) | ((b) * 4))) << 8) | 0xFF)

static const uint32_t palette[64] = {
/* A slightly modified version of Jare's FirePal. */
COLOR( 0,   0,   0), COLOR( 1,   0,   0), COLOR( 5,   0,   0), COLOR(10,   0,   0),
COLOR(15,   0,   0), COLOR(18,   0,   0), COLOR(21,   0,   0), COLOR(25,   0,   0),
COLOR(33,   3,   3), COLOR(40,   2,   2), COLOR(48,   2,   2), COLOR(55,   1,   1),
COLOR(63,   0,   0), COLOR(63,   0,   0), COLOR(63,   3,   0), COLOR(63,   7,   0),
COLOR(63,  10,   0), COLOR(63,  13,   0), COLOR(63,  16,   0), COLOR(63,  20,   0),
COLOR(63,  23,   0), COLOR(63,  26,   0), COLOR(63,  29,   0), COLOR(63,  33,   0),
COLOR(63,  36,   0), COLOR(63,  39,   0), COLOR(63,  39,   0), COLOR(63,  40,   0),
COLOR(63,  40,   0), COLOR(63,  41,   0), COLOR(63,  42,   0), COLOR(63,  42,   0),
COLOR(63,  43,   0), COLOR(63,  44,   0), COLOR(63,  44,   0), COLOR(63,  45,   0),
COLOR(63,  45,   0), COLOR(63,  46,   0), COLOR(63,  47,   0), COLOR(63,  47,   0),
COLOR(63,  48,   0), COLOR(63,  49,   0), COLOR(63,  49,   0), COLOR(63,  50,   0),
COLOR(63,  51,   0), COLOR(63,  51,   0), COLOR(63,  52,   0), COLOR(63,  53,   0),
COLOR(63,  53,   0), COLOR(63,  54,   0), COLOR(63,  55,   0), COLOR(63,  55,   0),
COLOR(63,  56,   0), COLOR(63,  57,   0), COLOR(63,  57,   0), COLOR(63,  58,   0),
COLOR(63,  58,   0), COLOR(63,  59,   0), COLOR(63,  60,   0), COLOR(63,  60,   0),
COLOR(63,  61,   0), COLOR(63,  62,   0), COLOR(63,  62,   0), COLOR(63,  63,   0),
};

static bool sdlInit = false;
static void *bgmBuffer = NULL;
static Mix_Chunk *backgroundMusic = NULL;
static SDL_Window* window = NULL;
static SDL_Renderer* renderer = NULL;
static SDL_Texture *texture = NULL;

static uint8_t *fire = NULL;
static uint8_t *prev_fire;
static uint32_t *framebuf;

extern void __init_wut_malloc();

// We don't do much mallocs, so WUTs fast malloc is overkill. Use coreinit instead
void __preinit_user(MEMHeapHandle *mem1, MEMHeapHandle *fg, MEMHeapHandle *mem2)
{
	__init_wut_malloc();
}

// A function to read a file, needed to load the audio
static inline size_t readFile(const char *path, void **buffer)
{
	FILE *file = fopen(path, "rb");
	if (file != NULL) {
		struct stat info;
		size_t filesize = fstat(fileno(file), &info) == -1 ? -1 : (size_t)(info.st_size);
		if (filesize != (size_t)-1) {
			*buffer = MEMAllocFromDefaultHeapEx(FS_ALIGN(filesize), 0x40);
			if (*buffer != NULL) {
				if(fread(*buffer, filesize, 1, file) == 1) {
					fclose(file);
					return filesize;
				}

				MEMFreeToDefaultHeap(*buffer);
			}
		}

		fclose(file);
	}

	*buffer = NULL;
	return 0;
}

// This function draws one frame. It's based on Javier "Jare" Arévalo's firedemo from 1993
static inline void drawFrame()
{
	uint32_t i;
	uint32_t sum;
	for (i = WIDTH + 1; i < (HEIGHT - 1) * WIDTH - 1; i++) {
		/* Average the eight neighbours. */
		// Left pixel of 2nd row
		sum = prev_fire[--i];
		// Top row
		i -= WIDTH;
		sum += prev_fire[i++];
		sum += prev_fire[i++];
		sum += prev_fire[i];
		// Bottom row
		i += (WIDTH * 2) - 2;
		sum += prev_fire[i++];
		sum += prev_fire[i++];
		sum += prev_fire[i];
		// Right pixel as second row + reset i
		i -= WIDTH;
		sum += prev_fire[i--];
		// Average result
		fire[i] = (uint8_t)(sum / 8);

		/* "Cool" the pixel if the two bottom bits of the
		sum are clear (somewhat random). For the bottom
		rows, cooling can overflow, causing "sparks". */
		if (!(sum & 3) &&
			(fire[i] > 0 || i >= (HEIGHT - 4) * WIDTH)) {
			fire[i]--;
		}
	}

	for (i = 0, sum = WIDTH; i < (HEIGHT - 1) * WIDTH; i++, sum++) {
		/* Remove dark pixels from the bottom rows */
		if (i >= (HEIGHT - 7) * WIDTH && fire[i] < 15) {
			fire[i] = 30 - fire[i];
		}

		/* Copy back and scroll up one row. */
		prev_fire[i] = fire[sum];

		/* Copy to framebuffer and map to RGBA. Use WHITE
		pixel in case of overflows and copy up one row. */
		framebuf[sum] = fire[i] < 64 ? palette[fire[i]] : COLOR(63, 63, 40);
	}
}

static void deinit()
{
	if (sdlInit) {
		if (bgmBuffer != NULL) {
			if (backgroundMusic != NULL) {
				if (window != NULL) {
					if (renderer != NULL) {
						if (texture != NULL) {
							SDL_DestroyTexture(texture);
							texture = NULL;
						}

						SDL_DestroyRenderer(renderer);
						renderer = NULL;
					}

					SDL_DestroyWindow(window);
					window = NULL;
				}

				Mix_FreeChunk(backgroundMusic);
				Mix_CloseAudio();
				backgroundMusic = NULL;
			}

			MEMFreeToDefaultHeap(bgmBuffer);
			bgmBuffer = NULL;
		}

		SDL_Quit();
		sdlInit = false;
	}

	if(fire != NULL)
	{
		MEMFreeToDefaultHeap(fire);
		fire = NULL;
	}
}

static void init() {
	fire = MEMAllocFromDefaultHeap((WIDTH * HEIGHT * 2) + (WIDTH * HEIGHT * sizeof(uint32_t)));
	if(fire == NULL)
		return;

	OSBlockSet(fire, 0x00, (WIDTH * HEIGHT * 2) + (WIDTH * HEIGHT * sizeof(uint32_t)));
	prev_fire = fire + (WIDTH * HEIGHT);
	framebuf = (uint32_t *)(prev_fire + (WIDTH * HEIGHT));

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) == 0) {
		sdlInit = true;
		if (Mix_Init(MIX_INIT_OGG)) {
			// Load audio
			size_t fs = readFile("/vol/content/audio/bg.ogg", &bgmBuffer);
			if (bgmBuffer != NULL) {
				if (Mix_OpenAudio(22050, AUDIO_S16MSB, 2, 4096) == 0) {
					SDL_RWops *rw = SDL_RWFromMem(bgmBuffer, fs);
					backgroundMusic = Mix_LoadWAV_RW(rw, true);
					if(backgroundMusic != NULL) {
						Mix_VolumeChunk(backgroundMusic, 100);
						if(Mix_PlayChannel(0, backgroundMusic, -1) == 0) {
							// Setup window
							window = SDL_CreateWindow(NULL, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 0, 0, SDL_WINDOW_FULLSCREEN_DESKTOP);
							if (window) {
								// Setup renderer
								renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
								if (renderer) {
									// Enable HQ upscaling
									SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
									SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

									texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, WIDTH, HEIGHT);
									if (texture) {
										// Draw 512 frames off-screen
										for (uint32_t i = 0; i < 512; i++) {
											drawFrame();
										}

										return;
									}
								}
							}
						}
					}
				}
			}
		}
	}

	deinit();
}

int main()
{
	ProcUIInit(OSSavesDone_ReadyToRelease);
	bool running = true;;

	while (running) {
		switch (ProcUIProcessMessages(true)) {
			case PROCUI_STATUS_IN_FOREGROUND:
				if (fire == NULL) {
					init();
				}
				if (fire != NULL) {
					drawFrame();
					SDL_UpdateTexture(texture, NULL, framebuf, WIDTH * sizeof(framebuf[0]));
					SDL_RenderCopy(renderer, texture, NULL, NULL);
					SDL_RenderPresent(renderer);
					SDL_Delay(1000 / FPS);
				}
				break;
			case PROCUI_STATUS_RELEASE_FOREGROUND:
// TODO: SDL bug
//				if (fire != NULL) {
//					deinit();
//				}
				ProcUIDrawDoneRelease();
				break;
			case PROCUI_STATUS_IN_BACKGROUND:
				OSSleepTicks(OSMillisecondsToTicks(20));
				break;
			case PROCUI_STATUS_EXITING:
				running = false;
				break;
		}
	}

// TODO: SDL bug
//	if (fire != NULL) {
//		deinit();
//	}

	ProcUIShutdown();
	return 0;
}
