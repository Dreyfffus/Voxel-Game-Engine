#include <iostream>
#include "Application.h"

int main()
{
	Application app;

	try {
		app.start();
	}
	catch (const std::exception& e) {
		std::cerr << e.what() << std::endl;
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}