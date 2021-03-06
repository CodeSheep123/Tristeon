﻿#include "StringUtils.h"
#include <iterator>
#include <vector>

std::vector<std::string> StringUtils::split(const std::string &s, char delim) {
	std::vector<std::string> elems;
	split(s, delim, std::back_inserter(elems));
	return elems;
}

std::string StringUtils::generateRandom(int length)
{
	static const char alphanum[] =
		"0123456789"
		"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
		"abcdefghijklmnopqrstuvwxyz";

	std::string output;

	for (int i = 0; i < length; ++i) {
		output.push_back(alphanum[rand() % (sizeof(alphanum) - 1)]);
	}

	return output;
}
