#pragma once

struct AVRational {
	int num;
	int den;
};

struct AVChannelLayout {
	int order;
	int nb_channels;
	union {
		uint64_t mask;
		void* map;
	} u;
	void* opaque;
};

struct AVFrame {
	uint8_t* data[8];
	int linesize[8];
	uint8_t** extended_data;
	int width, height;
	int nb_samples;
	int format;
	uint8_t reserved1[72];
	int sample_rate; // c0
	uint8_t reserved2[216];
	int nb_channels; // 19c
};
