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

// 最小値・最大値をまとめたもの
typedef struct {
    uint min;
    uint max;
} MinMax;

// ファイルからPGMイメージを読み出す
bool read_image(const char* filename, PNM* img) {
    FILE* f = fopen(filename, "r");
    if (f == NULL) {
        perror("read_image(fopen)");
        return false;
    }

    // ヘッダ読み出し
    int ret = fscanf(f, "%2s %zu %zu %hu", img->magic, &img->width, &img->height, &img->max);

    if (ret != 4) {
        fprintf(stderr, "read_image: cannot read the header\n");
        goto ERR;
    }

    // ピクセル読み出し
    for(size_t i = 0; i < img->height; i++) {
        for(size_t j = 0; j < img->width; j++) {
            uint tmp;
            if (fscanf(f, "%hu", &tmp) != 1) {
                fprintf(stderr, "read_image: cannot read a pixel\n");
                goto ERR;
            }
            if (tmp > img->max) {
                fprintf(stderr, "read_image: pixel \"%hu\" (%zu %zu) exceeds the max \"%hu\"\n", tmp, i, j, img->max);
                goto ERR;
            }
            img->image[i][j] = tmp;
        }
    }

    fclose(f);
    return true;

ERR:
    fclose(f);
    return false;
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

// 挿入ソート
// 参考；https://yaneurao.hatenadiary.com/entries/2009/11/26
void insertion_sort(uint *a, const size_t size){
    for (size_t i = 1; i < size; ++i) {
        uint buf = a[i];
        if (a[i-1] > buf) {
            size_t j = i;
            do {
                a[j] = a[j-1];
            } while (--j > 0 && a[j-1] > buf);
            a[j] = buf;
        }
    }
}

// メジアンフィルタ
void smooth_with_median(PNM* img) {
    uint new_img[HEIGHT_MAX][WIDTH_MAX];
    uint a[9];

    // メジアンフィルタをかけて結果を新しい配列に入れる
    for(size_t i = 1; i < img->height - 1; i++) {
        for(size_t j = 1; j < img->width - 1; j++) {
            a[0] = img->image[i-1][j-1];
            a[1] = img->image[i-1][j];
            a[2] = img->image[i-1][j+1];
            a[3] = img->image[i][j-1];
            a[4] = img->image[i][j];
            a[5] = img->image[i][j+1];
            a[6] = img->image[i+1][j-1];
            a[7] = img->image[i+1][j];
            a[8] = img->image[i+1][j+1];

            insertion_sort(a, 9);

            new_img[i][j] = a[4];
        }
    }

    // 結果を元の構造体に書き戻す
    for(size_t i = 1; i < img->height - 1; i++) {
        for(size_t j = 1; j < img->width - 1; j++) {
            img->image[i][j] = new_img[i][j];
        }
    }
}

// 画素値の最小・最大を探す
MinMax find_min_max(const PNM* img) {
    MinMax mm;
    mm.min = img->max;
    mm.max = 0;

    for(size_t i = 0; i < img->height; i++) {
        for(size_t j = 0; j < img->width; j++) {
            const uint val = img->image[i][j];
            if (val < mm.min) mm.min = val;
            if (val > mm.max) mm.max = val;
        }
    }

    return mm;
}

// コントラストを補正する
void adjust_contrast(MinMax mm, PNM* img) {
    const uint diff = mm.max - mm.min;

    /*
    以下の場合は補正を行っても値が変わらないので無視する
        - 全ての画素値が同一のとき
        - 最小値が 0 、最大値が img->max のとき
    */
    if (diff == 0 || (mm.max == img->max && mm.min == 0)) {
        fprintf(stderr, "adjust_contrast: no operation performed\n");
        return;
    }

    // 補正を実行
    for(size_t i = 0; i < img->height; i++) {
        for(size_t j = 0; j < img->width; j++) {
            uint* val = &(img->image[i][j]);
            *val = (img->max * (*val - mm.min)) / diff;
        }
    }
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

    smooth_with_median(&img);

    if (!write_image(argv[2], &img)) {
        fprintf(stderr, "main: error in writing image\n");
        return 1;
    }
}
