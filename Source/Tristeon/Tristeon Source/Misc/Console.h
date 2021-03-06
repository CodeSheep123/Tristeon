﻿#pragma once
#include <string>

namespace Tristeon
{
	namespace Misc
	{
		/**
		 * \brief Console defines basic printing/asserting behavior that will be logged to the editor or to a console
		 */
		class Console final
		{
		public:
			/**
			 * \brief Sets the standard console color
			 */
			static void init();

			/**
			 * \brief Throws an error if the given condition is not true
			 * \param condition The condition you expect to be true
			 * \param errorMessage The error message to be shown in the error pop-up
			 */
			static void t_assert(bool condition, std::string errorMessage);
			/**
			 * \brief Clears the console
			 */
			static void clear();

			/**
			 * \brief Writes the given data to the console
			 * \param data The data to be written to the console
			 */
			static void write(std::string data);

			/**
			 * \brief Writes a warning with the given data to the editor/console
			 * \param data The data to be written to the console
			 */
			static void warning(std::string data);
			/**
			 * \brief Opens an error pop-up window and stops the program.
			 * \param data The error message to be shown in the pop-up window
			 */
			static void error(std::string data);
		};
	}
}