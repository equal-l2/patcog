#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#define WIDTH_MAX 1024
#define HEIGHT_MAX 1024

#undef uint
// PGMでは各要素の値は16ビットで十分
typedef unsigned short uint;

typedef struct {
    char magic[3];
    size_t width;
    size_t height;
    uint max;
    uint image[HEIGHT_MAX][WIDTH_MAX];
} PNM;

// ファイルからPGMイメージを読み出す
bool read_image(const char* filename, PNM* img) {
    FILE* f = fopen(filename, "r");
    if (f == NULL) {
        perror("read_image(fopen)");
        return false;
    }

    // ヘッダ読み出し
    fscanf(f, "%2s %zu %zu %hu", img->magic, &img->width, &img->height, &img->max);

    // ピクセル読み出し
    for(size_t i = 0; i < img->height; i++) {
        for(size_t j = 0; j < img->width; j++) {
            uint tmp;
            fscanf(f, "%hu", &tmp);
            if (tmp > img->max) {
                fprintf(stderr, "read_image: pixel \"%hu\" (%zu %zu) exceeds the max \"%hu\"\n", tmp, i, j, img->max);
                tmp = img->max;
            }
            img->image[i][j] = tmp;
        }
    }

    fclose(f);
    return true;
}

// ファイルへPGMイメージを書き出す
bool write_image(const char* filename, const PNM* img) {
    FILE* f = fopen(filename, "w");
    if (f == NULL) {
        perror("write_image(fopen)");
        return false;
    }

    // ヘッダ書き出し
    fprintf(f, "%s\n%zu %zu\n%hu\n", img->magic, img->width, img->height, img->max);

    // ピクセル書き出し
    for(size_t i = 0; i < img->height; i++) {
        for(size_t j = 0; j < img->width; j++) {
            fprintf(f, "%hu ", img->image[i][j]);
        }
        fprintf(f, "\n");
    }

    fclose(f);

    return true;
}

int main(int argc, char** argv) {
    if (argc != 3) {
        fprintf(stderr, "%s [input] [output]\n", argv[0]);
        return 0;
    }

    PNM img;

    if (!read_image(argv[1], &img)) {
        fprintf(stderr, "main: error in reading image\n");
        return 1;
    }

    if (!write_image(argv[2], &img)) {
        fprintf(stderr, "main: error in writing image\n");
        return 1;
    }
}
