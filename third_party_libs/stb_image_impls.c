// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: Unlicense

#if __ARM_ARCH
#define STBI_NEON
#endif

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"
#undef STB_IMAGE_IMPLEMENTATION

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"
#undef STB_IMAGE_WRITE_IMPLEMENTATION

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb/stb_image_resize2.h"
#undef STB_IMAGE_RESIZE_IMPLEMENTATION
