#include "image_rgbe.h"
#include <cstdio>

extern "C" {
	#include "rgbe/rgbe.h"
}

namespace image_rgbe {

Data load(const wchar_t* path) {
	Data data = { nullptr, false };

	FILE* f = _wfopen(path, L"rb");
	do {
		if (f == nullptr) {
			break;
		}

		rgbe_header_info header;
		int w, h;
		int result = RGBE_ReadHeader(f, &w, &h, &header);
		if (result == RGBE_RETURN_FAILURE) {
			break;
		}

		float* rgb = new float[3 * w * h];
		result = RGBE_ReadPixels_RLE(f, rgb, w, h);
		if (result == RGBE_RETURN_FAILURE) {
			break;
		}

		data.rgb = rgb;
		data.w = w;
		data.h = h;
		data.valid = true;
	} while(0);
	fclose(f);
	return data;
}

} // namespace image_rgbe
