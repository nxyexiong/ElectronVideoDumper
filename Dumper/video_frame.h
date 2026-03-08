#pragma once
#include <vector>

struct GfxSize {
	int width;
	int height;
};

struct GfxPoint {
	int x;
	int y;
};

struct GfxRect {
	GfxPoint point;
	GfxSize size;
};

struct ColorPlaneLayout {
	size_t stride;
	size_t offset;
	size_t size;
};

struct VideoFrameLayout {
	int format;
	GfxSize coded_size;
	std::vector<ColorPlaneLayout> planes;
	bool is_multi_planar;
	size_t buffer_addr_align;
	uint64_t modifier;
};

struct SpanByte {
	uint8_t* ptr;
	size_t size;
};

struct VideoFrame {
	void* reserved1;
	VideoFrameLayout layout;
	void* wrapped_frame;
	void* intermediate_wrapped_frame;
	int storage_type; // must be STORAGE_OWNED_MEMORY = 3 to process
	GfxRect visible_rect;
	GfxSize natural_size;
	SpanByte data[4];
};
