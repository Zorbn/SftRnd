#include <iostream>
#include <array>
#include <cinttypes>
#include <chrono>
#include <cmath>

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

#include <immintrin.h>

const int32_t VIEW_WIDTH = 320;
const int32_t VIEW_HEIGHT = 240;
const int32_t VIEW_SCALE = 2;
const int32_t PIXEL_COUNT = VIEW_WIDTH * VIEW_HEIGHT;
const int32_t PIXEL_COMPONENTS = 4;
const int32_t PIXEL_STRIDE = VIEW_WIDTH * PIXEL_COMPONENTS;

#define PIXEL_FORMAT SDL_PIXELFORMAT_ARGB8888

class Image
{
public:
    SDL_Surface *surface;

    Image(const std::string &path)
    {
        auto loadedSurface = IMG_Load(path.c_str());

        if (!loadedSurface)
        {
            std::cout << "Failed to load image: " << path << "\n";
        }

        surface = SDL_ConvertSurfaceFormat(loadedSurface, PIXEL_FORMAT, 0);
        SDL_FreeSurface(loadedSurface);
    }

    ~Image()
    {
        SDL_FreeSurface(surface);
    }
};

inline uint32_t getImagePixel(const Image &image, int32_t x, int32_t y)
{
    auto width = image.surface->w;
    auto pixels = reinterpret_cast<uint32_t *>(image.surface->pixels);
    return pixels[x + y * width];
}

inline void setPixel(std::array<uint32_t, PIXEL_COUNT> &pixels, int32_t x, int32_t y, uint32_t color)
{
    auto i = x + y * VIEW_WIDTH;
    auto oldColor = pixels[i];
    auto alpha = (color & 0xff000000) > 0;
    pixels[i] = oldColor * !alpha + color * alpha;
}

void copySprite(std::array<uint32_t, PIXEL_COUNT> &pixels, int32_t x, int32_t y, int32_t width, int32_t height, const Image &image, int32_t texX, int32_t texY)
{
    int32_t startX = std::max(x, 0);
    int32_t startY = std::max(y, 0);
    int32_t endX = std::min(x + width, VIEW_WIDTH);
    int32_t endY = std::min(y + height, VIEW_HEIGHT);
    int32_t spanX = endX - startX;
    int32_t spanY = endY - startY;

    auto srcPixels = reinterpret_cast<uint32_t *>(image.surface->pixels);

    for (int32_t iy = 0; iy < spanY; iy++)
    {
        memcpy(&pixels[startX + (startY + iy) * VIEW_WIDTH], &srcPixels[texX + (texY + iy) * image.surface->w], spanX * sizeof(uint32_t));
    }
}

#ifdef USE_AVX
const __m256i zero8 = _mm256_set1_epi32(0);

void blitSprite8(std::array<uint32_t, PIXEL_COUNT> &pixels, int32_t x, int32_t y, int32_t width, int32_t height, const Image &image, int32_t texX, int32_t texY, bool flipX = false, bool flipY = false)
{
    int32_t startX = std::max(x, 0);
    int32_t startY = std::max(y, 0);
    int32_t endX = std::min(x + width, VIEW_WIDTH);
    int32_t endY = std::min(y + height, VIEW_HEIGHT);
    int32_t spanX = endX - startX;
    int32_t spanY = endY - startY;
    int32_t texStartY = texY + flipY * height;
    int32_t texDirectionY = -(2 * flipY - 1);

    auto srcPixels = reinterpret_cast<uint32_t *>(image.surface->pixels);

    for (int32_t iy = 0; iy < spanY; iy++)
    {
        for (int32_t ix = 0; ix < spanX; ix += 8)
        {
            // This method follows the bit blit algorithm: https://en.wikipedia.org/wiki/Bit_blit.
            // The idea is to AND the destination pixels with a black and white mask (black where
            // the sprite should be opaque, white where the sprite should be transparent). Then
            // OR the source pixels with the mask to get the correct values for each pixel.
            // Some extra work is done by comparing the source pixel to zero in order to avoid
            // creating a mask manually.

            // Destination pixel:
            auto dst = _mm256_loadu_epi32(&pixels[(startX + ix) + (startY + iy) * VIEW_WIDTH]);
            // Source pixel:
            auto src = _mm256_loadu_epi32(&srcPixels[(texX + ix) + (texStartY + texDirectionY * iy) * image.surface->w]);

            if (flipX)
            {
                // Reverse the source:
                src = _mm256_shuffle_epi32(src, _MM_SHUFFLE(0, 1, 2, 3));
                src = _mm256_permute2f128_si256(src, src, 1);
            }

            // Non transparent pixels (alpha == 0) get filled with black:
            auto mask = _mm256_and_epi32(dst, _mm256_cmpeq_epi32(zero8, src));
            // Write the sprites actual pixels onto the masked pixels:
            auto result = _mm256_or_epi32(src, mask);
            _mm256_storeu_epi32(&pixels[(startX + ix) + (startY + iy) * VIEW_WIDTH], result);
        }
    }
}
#endif

#ifdef USE_SSE
const __m128i zero4 = _mm_set1_epi32(0);

void blitSprite4(std::array<uint32_t, PIXEL_COUNT> &pixels, int32_t x, int32_t y, int32_t width, int32_t height, const Image &image, int32_t texX, int32_t texY, bool flipX = false, bool flipY = false)
{
    int32_t startX = std::max(x, 0);
    int32_t startY = std::max(y, 0);
    int32_t endX = std::min(x + width, VIEW_WIDTH);
    int32_t endY = std::min(y + height, VIEW_HEIGHT);
    int32_t spanX = endX - startX;
    int32_t spanY = endY - startY;
    int32_t texStartY = texY + flipY * height;
    int32_t texDirectionY = -(2 * flipY - 1);

    auto srcPixels = reinterpret_cast<uint32_t *>(image.surface->pixels);

    for (int32_t iy = 0; iy < spanY; iy++)
    {
        for (int32_t ix = 0; ix < spanX; ix += 4)
        {
            auto dst = _mm_loadu_epi32(&pixels[(startX + ix) + (startY + iy) * VIEW_WIDTH]);
            auto src = _mm_loadu_epi32(&srcPixels[(texX + ix) + (texStartY + texDirectionY * iy) * image.surface->w]);

            if (flipX)
            {
                src = _mm_shuffle_epi32(src, _MM_SHUFFLE(0, 1, 2, 3));
            }

            auto mask = _mm_and_epi32(dst, _mm_cmpeq_epi32(zero4, src));
            auto result = _mm_or_epi32(src, mask);
            _mm_storeu_epi32(&pixels[(startX + ix) + (startY + iy) * VIEW_WIDTH], result);
        }
    }
}
#endif

void drawSprite(std::array<uint32_t, PIXEL_COUNT> &pixels, int32_t x, int32_t y, int32_t width, int32_t height, const Image &image, int32_t texX, int32_t texY, bool flipX = false, bool flipY = false)
{
#ifdef USE_AVX
    if (width % 8 == 0) {
        blitSprite8(pixels, x, y, width, height, image, texX, texY, flipX, flipY);
        return;
    }
#endif

#ifdef USE_SSE
    if (width % 4 == 0) {
        blitSprite4(pixels, x, y, width, height, image, texX, texY, flipX, flipY);
        return;
    }
#endif

    int32_t startX = std::max(x, 0);
    int32_t startY = std::max(y, 0);
    int32_t endX = std::min(x + width, VIEW_WIDTH);
    int32_t endY = std::min(y + height, VIEW_HEIGHT);
    int32_t spanX = endX - startX;
    int32_t spanY = endY - startY;
    int32_t texStartX = texX + flipX * width;
    int32_t texStartY = texY + flipY * height;
    int32_t texDirectionX = -(2 * flipX - 1);
    int32_t texDirectionY = -(2 * flipY - 1);

    for (int32_t iy = 0; iy < spanY; iy++)
    {
        for (int32_t ix = 0; ix < spanX; ix++)
        {
            auto color = getImagePixel(image,
                                       texStartX + texDirectionX * ix,
                                       texStartY + texDirectionY * iy);
            setPixel(pixels, startX + ix, startY + iy, color);
        }
    }
}

void drawRect(std::array<uint32_t, PIXEL_COUNT> &pixels, int32_t x, int32_t y, int32_t width, int32_t height, uint32_t color)
{
    int32_t startX = std::max(x, 0);
    int32_t startY = std::max(y, 0);
    int32_t endX = std::min(x + width, VIEW_WIDTH);
    int32_t endY = std::min(y + height, VIEW_HEIGHT);
    int32_t spanX = endX - startX;
    int32_t spanY = endY - startY;

    for (int32_t iy = 0; iy < spanY; iy++)
    {
        for (int32_t ix = 0; ix < spanX; ix++)
        {
            setPixel(pixels, startX + ix, startY + iy, color);
        }
    }
}

int main(int argc, char **argv)
{

    if (SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        std::cout << "Failed to initialize SDL!\n";
        exit(0);
    }

    if (!(IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG))
    {
        std::cout << "Failed to initialize SDL_image!\n";
        exit(0);
    }

    auto window = SDL_CreateWindow("SftRnd",
                                   SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                   VIEW_WIDTH * VIEW_SCALE,
                                   VIEW_HEIGHT * VIEW_SCALE,
                                   SDL_WINDOW_RESIZABLE);

    if (!window)
    {
        std::cout << "Failed to create window!\n";
        exit(0);
    }

    auto renderer = SDL_CreateRenderer(window, -1, 0 /*SDL_RENDERER_PRESENTVSYNC*/);
    SDL_RenderSetLogicalSize(renderer, VIEW_WIDTH, VIEW_HEIGHT);
    SDL_RenderSetIntegerScale(renderer, SDL_TRUE);
    auto screenTexture = SDL_CreateTexture(renderer,
                                           PIXEL_FORMAT, SDL_TEXTUREACCESS_STREAMING, VIEW_WIDTH, VIEW_HEIGHT);
    static std::array<uint32_t, PIXEL_COUNT> pixels;
    pixels.fill(0xffffffff);

    Image image("tiles.png");

    const int32_t mapWidth = VIEW_WIDTH / 16;
    const int32_t mapHeight = VIEW_HEIGHT / 16;

    auto totalDelta = 0.0f;
    auto totalFrames = 0;

    auto lastTime = std::chrono::high_resolution_clock::now();
    auto isRunning = true;
    while (isRunning)
    {
        // auto currentTime = std::chrono::high_resolution_clock::now();
        // auto deltaTime = static_cast<float>((currentTime - lastTime).count()) * 0.000001f;
        // lastTime = currentTime;
        // std::cout << deltaTime << "\n";

        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_QUIT)
            {
                isRunning = false;
            }

            if (event.type == SDL_KEYDOWN)
            {

            }

            if (event.type == SDL_KEYUP)
            {

            }
        }

        lastTime = std::chrono::high_resolution_clock::now();

        for (int32_t y = 0; y < mapHeight; y++)
        {
            for (int32_t x = 0; x < mapWidth; x++)
            {
                drawSprite(pixels, x * 16, y * 16, 16, 16, image, 0, 0);
                drawSprite(pixels, x * 16, y * 16, 16, 16, image, 0, 40);
            }
        }

        auto currentTime = std::chrono::high_resolution_clock::now();
        auto deltaTime = static_cast<float>((currentTime - lastTime).count()) * 0.000001f;
        std::cout << deltaTime << "\n";

        totalDelta += deltaTime;
        totalFrames++;

        SDL_UpdateTexture(screenTexture, nullptr, reinterpret_cast<void *>(&pixels[0]), PIXEL_STRIDE);
        SDL_RenderCopy(renderer, screenTexture, nullptr, nullptr);
        SDL_RenderPresent(renderer);
    }

    std::cout << "Average frametime: " << totalDelta / totalFrames << "\n";

    return 0;
}
