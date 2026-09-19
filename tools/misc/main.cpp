
/* Copyright 2023 OneOfEleven
 * https://github.com/DualTachyon
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vector>

	// ************************************************************************
// "rotate_font()" has nothing to do with this program at all, I just needed
// to write a bit of code to rotate some fonts I've drawn

void rotate_font(const char *filename1, const char *filename2)
{
	std::vector <uint8_t> data;

	if (filename1 == NULL || filename2 == NULL)
		return;

	// ****************************
	// load the file

	FILE *file = fopen(filename1, "rb");
	if (file == NULL)
		return;

	if (fseek(file, 0, SEEK_END) != 0)
	{
		fclose(file);
		return;
	}
	const size_t file_size = ftell(file);
	if (file_size <= 0)
	{
		fclose(file);
		return;
	}
	if (fseek(file, 0, SEEK_SET) != 0)
	{
		fclose(file);
		return;
	}

	data.resize(file_size);

	const size_t bytes_loaded = fread(&data[0], 1, file_size, file);

	fclose(file);

	if (bytes_loaded != file_size)
		return;

	// ***************************
	// rotate the font 90-deg clockwise

	for (unsigned int i = 0; i <= (data.size() - 8); i += 8)
	{
		uint8_t c1[8];
		uint8_t c2[8];
		memcpy(c1, &data[i], 8);
		memset(c2, 0, 8);
		for (unsigned int k = 0; k < 8; k++)
		{
			uint8_t b = c1[k];
			for (unsigned int m = 0; m < 8; m++)
			{
				if (b & 0x80)
					c2[m] |= 1u << k;
				b <<= 1;
			}
		}
		memcpy(&data[i], c2, 8);
	}

	// ***************************
	// save the file

	file = fopen(filename2, "wt");
	if (file == NULL)
		return;

	fprintf(file, "const uint8_t gFontSmall[95][7] =\n");
	fprintf(file, "{\n");

	for (unsigned int i = 0; i < data.size(); )
	{
		char s[1024];
		memset(s, 0, sizeof(s));

//		for (unsigned int k = 0; k < 8 && i < data.size(); k++)
		for (unsigned int k = 0; k < 7 && i < data.size(); k++)
		{
			char s2[16];
			sprintf(s2, "0x%02X", data[i++]);

			if (k == 0)
				strcat(s, "\t{");

//			if (k < 7)
			if (k < 6)
			{
				strcat(s,  s2);
				strcat(s, ", ");
			}
			else
			{
				strcat(s, s2);
				strcat(s, "},\n");
			}
		}

		i++;

		fprintf(file, "%s", s);
	}

	fprintf(file, "};\n");

	fclose(file);

	// ***************************
}

#pragma argsused
int main(int argc, char* argv[])
{
	rotate_font("uv-k5_small.bin",      "uv-k5_small.c");
	rotate_font("uv-k5_small_bold.bin", "uv-k5_small_bold.c");

	return 0;
}
