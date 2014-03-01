#include <iostream>

int wmain(int argc, wchar_t* argv[]) {
	if (argc != 3) {
		std::cout << "Usage:" << std::endl;
		std::cout << "rgbm-tool.exe <hdr-image> <tga-output>" << std::endl;
		return 1;
	}
	// TODO:
	return 0;
}
