#pragma once

namespace image_rgbe {

struct Data {
	float* rgb;
	unsigned w, h;
	bool valid;
};

Data load(const wchar_t* path);

} // namespace image_rgbe
