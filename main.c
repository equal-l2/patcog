#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIDTH_MAX 4096
#define HEIGHT_MAX 4096

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
    int ret = fscanf(f, "%2s %zu %zu %hu", img->magic, &img->width, &img->height, &img->max);

    if (ret != 4) {
        fprintf(stderr, "read_image: cannot read the header\n");
        goto ERR;
    }

    if (img->width > WIDTH_MAX || img->height > HEIGHT_MAX) {
        fprintf(stderr, "read_image: image is too big\n");
        goto ERR;
    }

    if (img->magic[1] != '2') {
        fprintf(stderr, "read_image: image is not PGM(ASCII)\n");
        goto ERR;
    }

    // 画素読み出し
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

    // 画素書き出し
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
// 参考: https://yaneurao.hatenadiary.com/entries/2009/11/26
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
    // 画素配列は大きいのでヒープに置く
    // 二次元配列の確保は危険で面倒なのでPNM構造体で代用
    PNM* new_img = malloc(sizeof(PNM));

    // メジアンフィルタをかけて結果を新しい配列に入れる
    for(size_t i = 1; i < img->height - 1; i++) {
        for(size_t j = 1; j < img->width - 1; j++) {
            uint a[] = {
                img->image[i-1][j-1],
                img->image[i-1][j],
                img->image[i-1][j+1],
                img->image[i][j-1],
                img->image[i][j],
                img->image[i][j+1],
                img->image[i+1][j-1],
                img->image[i+1][j],
                img->image[i+1][j+1]
            };

            insertion_sort(a, sizeof(a)/sizeof(a[0]));

            new_img->image[i][j] = a[4];
        }
    }

    // 結果を元の構造体に書き戻す
    for(size_t i = 1; i < img->height - 1; i++) {
        for(size_t j = 1; j < img->width - 1; j++) {
            img->image[i][j] = new_img->image[i][j];
        }
    }

    free(new_img);
}

// モザイク処理
void pixelize(PNM* img, size_t block_size) {
    for(size_t i = 0; i < img->height; i += block_size) {
        for(size_t j = 0; j < img->width; j += block_size) {
            // ブロック内の画素値の平均を求める
            unsigned long long avg = 0;
            size_t cnt = 0;
            for(size_t k = 0; k < block_size && i+k < img->height; k++) {
                for(size_t l = 0; l < block_size && j+l < img->width; l++) {
                    avg += img->image[i+k][j+l];
                    cnt++;
                }
            }
            avg /= cnt;

            // 求めた平均値でブロック全体を上書きする
            for(size_t k = 0; k < block_size && i+k < img->height; k++) {
                for(size_t l = 0; l < block_size && j+l < img->width; l++) {
                    img->image[i+k][j+l] = (uint)avg;
                }
            }
        }
    }
}

// 最小値・最大値をまとめたもの
typedef struct {
    uint min;
    uint max;
} MinMax;

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

// スケール処理
bool scale(PNM* img, double height_factor, double width_factor) {
    // スケール後の画像の大きさは、係数を乗じて四捨五入する
    const double new_height = round(height_factor * img->height);
    const double new_width = round(width_factor * img->width);

    // スケール前後の画像の大きさを出力
    fprintf(stderr, "scale: %zdx%zd -> %.0fx%.0f\n", img->height, img->width, new_height, new_width);

    // スケール後に大きすぎたり0になったりする場合は中止
    if (new_height > (double)HEIGHT_MAX || new_width > (double)WIDTH_MAX) {
        fprintf(stderr, "read_image: cannot scale, resulting image will be too big\n");
        return false;
    }
    if (new_height == 0 || new_width == 0) {
        fprintf(stderr, "read_image: cannot scale, resulting image will be zero-sized\n");
        return false;
    }

    PNM* new_img = malloc(sizeof(PNM));
    strcpy(new_img->magic, img->magic);
    new_img->height = (size_t)new_height;
    new_img->width = (size_t)new_width;
    new_img->max = img->max;

    for(size_t i = 0; i < new_img->height; i++) {
        for(size_t j = 0; j < new_img->width; j++) {
            double tmp;

            // 補間原点：スケール後画像の対象画素を、スケール前画像空間に戻した際の実数座標の整数部

            const double h_dist = modf(i/height_factor, &tmp); // 補間原点からの高さ方向の距離
            const size_t h_base = (size_t)tmp; // 補間原点の高さ方向座標

            const double w_dist = modf(j/width_factor, &tmp); // 補間原点からの幅方向の距離
            const size_t w_base = (size_t)tmp; // 補間原点の幅方向座標

            if (h_base == img->height-1 || w_base == img->width-1) {
                // 補間原点が画像の端であるとき
                // 補間できないので補間原点の画素値でとりあえず埋めておく
                new_img->image[i][j] = img->image[h_base][w_base];
            } else {
                new_img->image[i][j] = (uint)(
                        img->image[h_base][w_base]*(1-h_dist)*(1-w_dist) +
                        img->image[h_base+1][w_base]*h_dist*(1-w_dist) +
                        img->image[h_base][w_base+1]*(1-h_dist)*w_dist +
                        img->image[h_base+1][w_base+1]*h_dist*w_dist
                );
            }
        }
    }

    *img = *new_img;

    free(new_img);

    return true;
}

// 文字列から double 型の数を取り出す
bool get_double(const char* str, double* ret) {
    char* c = NULL;
    const double d = strtod(str, &c);
    if (str == c) {
        fprintf(stderr, "get_double: input is not a valid double\n");
        return false;
    }
    if (d == HUGE_VAL || d == -HUGE_VAL) {
        fprintf(stderr, "get_double: input is out of range\n");
        return false;
    }
    *ret = d;
    return true;
}

static inline double deg_to_rad(double deg) {
    // pi/180;
    const static double factor = 0.01745329251994329576923690768488612713442;
    return deg * factor;
}

// (x0, y0) を中心に角度 theta だけ回転
// theta は radian
bool rotate(PNM* img, double theta, double x0, double y0) {
    PNM* new_img = malloc(sizeof(PNM));
    strcpy(new_img->magic, img->magic);
    new_img->height = img->height;
    new_img->width = img->width;
    new_img->max = img->max;

    const double sint = sin(theta);
    const double cost = cos(theta);
    for(size_t i = 0; i < new_img->height; i++) {
        for(size_t j = 0; j < new_img->width; j++) {
            // 逆変換で元の座標を算出する
            const double x_orig = cost*(j-x0)+sint*(i-y0)+x0;
            const double y_orig = -sint*(j-x0)+cost*(i-y0)+y0;

            if (
                0 <= x_orig && x_orig <= (new_img->width - 1) &&
                0 <= y_orig && y_orig <= (new_img->height - 1)
            ) {
                /* 補間処理 */
                double tmp;
                const double h_dist = modf(y_orig, &tmp); // 補間原点からの高さ方向の距離
                const size_t h_base = (size_t)tmp; // 補間原点の高さ方向座標

                const double w_dist = modf(x_orig, &tmp); // 補間原点からの幅方向の距離
                const size_t w_base = (size_t)tmp; // 補間原点の幅方向座標

                if (h_base == img->height-1 || w_base == img->width-1) {
                    // 補間原点が画像の端であるとき
                    // 補間できないので0を入れておく
                    new_img->image[i][j] = 0;
                } else {
                    new_img->image[i][j] = (uint)(
                        img->image[h_base][w_base]*(1-h_dist)*(1-w_dist) +
                        img->image[h_base+1][w_base]*h_dist*(1-w_dist) +
                        img->image[h_base][w_base+1]*(1-h_dist)*w_dist +
                        img->image[h_base+1][w_base+1]*h_dist*w_dist
                    );
                }
            } else {
                // 元の点は存在しないので0を入れておく
                new_img->image[i][j] = 0;
            }
        }
    }

    *img = *new_img;
    free(new_img);

    return true;
}

/*
 Affine transformation: (x0, y0) -> (X, Y)
 / \   /   \ /  \   / \
 |X| = |a b| |x0| + |c|
 |Y|   |d e| |y0|   |f|
 \ /   \   / \  /   \ /
 */
typedef struct {
    double a;
    double b;
    double c;
    double d;
    double e;
    double f;
} AffineArgs;

// アフィン変換
bool affine_trans(PNM* img, AffineArgs args) {
    // 変換行列の行列式
    const double det = args.a*args.e-args.b*args.d;

    if (det == 0) {
        fprintf(stderr, "affine_trans: determinant is zero\n");
        return false;
    }

    PNM* new_img = malloc(sizeof(PNM));
    strcpy(new_img->magic, img->magic);
    new_img->height = img->height;
    new_img->width = img->width;
    new_img->max = img->max;

    for(size_t i = 0; i < new_img->height; i++) {
        for(size_t j = 0; j < new_img->width; j++) {
            // 逆変換で元の座標を算出する
            const double x_orig = (args.e*(j-args.c)-args.b*(i-args.f))/det;
            const double y_orig = (-args.d*(j-args.c)+args.a*(i-args.f))/det;

            if (
                0 <= x_orig && x_orig <= (new_img->width - 1) &&
                0 <= y_orig && y_orig <= (new_img->height - 1)
            ) {
                /* 補間処理 */
                double tmp;
                const double h_dist = modf(y_orig, &tmp); // 補間原点からの高さ方向の距離
                const size_t h_base = (size_t)tmp; // 補間原点の高さ方向座標

                const double w_dist = modf(x_orig, &tmp); // 補間原点からの幅方向の距離
                const size_t w_base = (size_t)tmp; // 補間原点の幅方向座標

                if (h_base == img->height-1 || w_base == img->width-1) {
                    // 補間原点が画像の端であるとき
                    // 補間できないので0を入れておく
                    new_img->image[i][j] = 0;
                } else {
                    new_img->image[i][j] = (uint)(
                        img->image[h_base][w_base]*(1-h_dist)*(1-w_dist) +
                        img->image[h_base+1][w_base]*h_dist*(1-w_dist) +
                        img->image[h_base][w_base+1]*(1-h_dist)*w_dist +
                        img->image[h_base+1][w_base+1]*h_dist*w_dist
                    );
                }
            } else {
                // 元の点は存在しないので0を入れておく
                new_img->image[i][j] = 0;
            }
        }
    }

    *img = *new_img;
    free(new_img);

    return true;
}

int main(int argc, char** argv) {

#define GET_DOUBLE_ARG(n, to, arg_desc)\
    if (!get_double(argv[n], to)) {\
        fprintf(stderr, "main: error in parsing "arg_desc"\n");\
        return 1;\
    }

    const int nargs = 9;
    if (argc != nargs) {
        fprintf(stderr, "expected %d arguments, got %d\n", nargs, argc);
        fprintf(stderr, "%s [input] [output] [affine args(a~f)]\n", argv[0]);
        return 0;
    }

    AffineArgs args;

    GET_DOUBLE_ARG(3, &args.a, "affine arg a");
    GET_DOUBLE_ARG(4, &args.b, "affine arg b");
    GET_DOUBLE_ARG(5, &args.c, "affine arg c");
    GET_DOUBLE_ARG(6, &args.d, "affine arg d");
    GET_DOUBLE_ARG(7, &args.e, "affine arg e");
    GET_DOUBLE_ARG(8, &args.f, "affine arg f");

    // 画素配列は大きいのでヒープに置く
    PNM* img = malloc(sizeof(PNM));

    if (!read_image(argv[1], img)) {
        fprintf(stderr, "main: error in reading image\n");
        return 1;
    }

    const bool res = affine_trans(img, args);

    if (!res) {
        fprintf(stderr, "main: error in processing\n");
        return 1;
    }

    if (!write_image(argv[2], img)) {
        fprintf(stderr, "main: error in writing image\n");
        return 1;
    }

    free(img);
}
