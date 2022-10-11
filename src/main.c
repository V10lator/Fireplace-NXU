#include <coreinit/memdefaultheap.h>
#include <gx2/event.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_events.h>
#include <SDL2/SDL_mixer.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sysapp/launch.h>

#include <whb/proc.h>

#define WIDTH 640
#define HEIGHT 54
#define WIN_WIDTH 1280
#define WIN_HEIGHT 720
#define FPS 20

#define FS_ALIGN(x) ((x + 0x3F) & ~(0x3F))

static const uint32_t palette[256] = {
/* Jare's original FirePal. */
#define C(r,g,b) ((((uint32_t)((((r) * 4) << 16) | ((g) * 4 << 8) | ((b) * 4))) << 8) | 0xff)
C( 0,   0,   0), C( 1,   0,   0), C( 5,   0,   0), C(10,   0,   0),
C(15,   0,   0), C(18,   0,   0), C(21,   0,   0), C(25,   0,   0),
C(33,   3,   3), C(40,   2,   2), C(48,   2,   2), C(55,   1,   1),
C(63,   0,   0), C(63,   0,   0), C(63,   3,   0), C(63,   7,   0),
C(63,  10,   0), C(63,  13,   0), C(63,  16,   0), C(63,  20,   0),
C(63,  23,   0), C(63,  26,   0), C(63,  29,   0), C(63,  33,   0),
C(63,  36,   0), C(63,  39,   0), C(63,  39,   0), C(63,  40,   0),
C(63,  40,   0), C(63,  41,   0), C(63,  42,   0), C(63,  42,   0),
C(63,  43,   0), C(63,  44,   0), C(63,  44,   0), C(63,  45,   0),
C(63,  45,   0), C(63,  46,   0), C(63,  47,   0), C(63,  47,   0),
C(63,  48,   0), C(63,  49,   0), C(63,  49,   0), C(63,  50,   0),
C(63,  51,   0), C(63,  51,   0), C(63,  52,   0), C(63,  53,   0),
C(63,  53,   0), C(63,  54,   0), C(63,  55,   0), C(63,  55,   0),
C(63,  56,   0), C(63,  57,   0), C(63,  57,   0), C(63,  58,   0),
C(63,  58,   0), C(63,  59,   0), C(63,  60,   0), C(63,  60,   0),
C(63,  61,   0), C(63,  62,   0), C(63,  62,   0), C(63,  63,   0),
/* Followed by "white heat". */
#define W C(63,63,63)
W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W,
W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W,
W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W,
W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W,
W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W,
W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W,
W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W,
W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W
#undef W
#undef C
};

static uint8_t fire[WIDTH * HEIGHT];
static uint8_t prev_fire[WIDTH * HEIGHT];
static uint32_t framebuf[WIDTH * HEIGHT];

static size_t getFilesize(FILE *fp)
{
	struct stat info;
	return fstat(fileno(fp), &info) == -1 ? -1 : (size_t)(info.st_size);
}

size_t readFile(const char *path, void **buffer)
{
	FILE *file = fopen(path, "rb");
	if (file != NULL) {
		size_t filesize = getFilesize(file);
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

int main()
{
	WHBProcInit();

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) == 0) {
		if (Mix_Init(MIX_INIT_OGG)) {
			void *bgmBuffer;
			size_t fs = readFile("/vol/content/audio/bg.ogg", &bgmBuffer);
			if (bgmBuffer != NULL) {
				if (Mix_OpenAudio(22050, AUDIO_S16MSB, 2, 4096) == 0) {
					SDL_RWops *rw = SDL_RWFromMem(bgmBuffer, fs);
					Mix_Chunk *backgroundMusic = Mix_LoadWAV_RW(rw, true);
					if(backgroundMusic != NULL) {
						Mix_VolumeChunk(backgroundMusic, 100);
						if(Mix_PlayChannel(0, backgroundMusic, -1) == 0) {
							//Setup window
							SDL_Window* window = SDL_CreateWindow(NULL, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 0, 0, SDL_WINDOW_FULLSCREEN_DESKTOP);
							if (window) {
								//Setup renderer
								SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
								if (renderer) {
									SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
									SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

									SDL_Texture * texture  = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, WIDTH, HEIGHT);
									if (texture) {
										int i;
										uint32_t sum;
										uint8_t avg;
										while(WHBProcIsRunning()) {
											for (i = WIDTH + 1; i < (HEIGHT - 1) * WIDTH - 1; i++) {
												/* Average the eight neighbours. */
												sum = prev_fire[i - WIDTH - 1] +
													prev_fire[i - WIDTH    ] +
													prev_fire[i - WIDTH + 1] +
													prev_fire[i - 1] +
													prev_fire[i + 1] +
													prev_fire[i + WIDTH - 1] +
													prev_fire[i + WIDTH    ] +
													prev_fire[i + WIDTH + 1];
												avg = (uint8_t)(sum / 8);

												/* "Cool" the pixel if the two bottom bits of the
												sum are clear (somewhat random). For the bottom
												rows, cooling can overflow, causing "sparks". */
												if (!(sum & 3) &&
													(avg > 0 || i >= (HEIGHT - 4) * WIDTH)) {
														avg--;
												}
												fire[i] = avg;
											}

											for (i = 0; i < (HEIGHT - 1) * WIDTH; i++) {
												/* Copy back and scroll up one row.
												The bottom row is all zeros, so it can be skipped. */
												prev_fire[i] = fire[i + WIDTH];

												/* Remove dark pixels from the bottom rows (except again the
												bottom row which is all zeros). */
												if (i >= (HEIGHT - 7) * WIDTH && fire[i] < 15) {
													fire[i] = 22 - fire[i];
												}

												/* Copy to framebuffer and map to RGBA, scrolling up one row. */
												framebuf[i + WIDTH] = palette[fire[i]];
											}

											/* Update the texture and render it. */
											SDL_UpdateTexture(texture, NULL, framebuf, WIDTH * sizeof(framebuf[0]));
											SDL_RenderCopy(renderer, texture, NULL, NULL);
											SDL_RenderPresent(renderer);

											SDL_Delay(1000 / FPS);
										}

										SDL_DestroyTexture(texture);
									}

									SDL_DestroyRenderer(renderer);
								}

								SDL_DestroyWindow(window);
							}

							Mix_HaltChannel(0);
						}

						Mix_FreeChunk(backgroundMusic);
					}

					Mix_CloseAudio();
				}

				MEMFreeToDefaultHeap(bgmBuffer);
			}
		}

		SDL_Quit();
	}

	return 0;
}
