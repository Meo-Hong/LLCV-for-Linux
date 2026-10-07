#include "platform/PngImage.h"

#include <png.h>

namespace llcv::platform {

bool EncodePngRgb(const std::vector<uint8_t>& rgba, int width, int height,
                  std::vector<uint8_t>& png, std::string& error) {
    if (width <= 0 || height <= 0) {
        error = "invalid image size";
        return false;
    }
    const size_t pixels = static_cast<size_t>(width) * static_cast<size_t>(height);
    if (rgba.size() < pixels * 4) {
        error = "image buffer is too small";
        return false;
    }
    std::vector<uint8_t> rgb(pixels * 3);
    for (size_t i = 0; i < pixels; ++i) {
        rgb[i * 3] = rgba[i * 4];
        rgb[i * 3 + 1] = rgba[i * 4 + 1];
        rgb[i * 3 + 2] = rgba[i * 4 + 2];
    }

    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    image.width = static_cast<png_uint_32>(width);
    image.height = static_cast<png_uint_32>(height);
    image.format = PNG_FORMAT_RGB;

    png_alloc_size_t size = 0;
    if (!png_image_write_to_memory(&image, nullptr, &size, 0, rgb.data(), 0, nullptr)) {
        error = image.message;
        return false;
    }
    png.resize(size);
    if (!png_image_write_to_memory(&image, png.data(), &size, 0, rgb.data(), 0, nullptr)) {
        error = image.message;
        png.clear();
        return false;
    }
    png.resize(size);
    return true;
}

bool LoadPngRgba(const std::filesystem::path& path, std::vector<uint8_t>& pixels,
                 int& width, int& height) {
    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_file(&image, path.c_str())) return false;
    image.format = PNG_FORMAT_RGBA;
    pixels.resize(PNG_IMAGE_SIZE(image));
    if (!png_image_finish_read(&image, nullptr, pixels.data(), 0, nullptr)) {
        png_image_free(&image);
        pixels.clear();
        return false;
    }
    width = static_cast<int>(image.width);
    height = static_cast<int>(image.height);
    return true;
}

}
