#include <SDL3/SDL.h>

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    return 1;
  }

  SDL_Window *window = SDL_CreateWindow("SDL3 Hello", 640, 480, 0);
  if (!window) {
    SDL_Quit();
    return 1;
  }

  SDL_Delay(500);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
