#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>

int utf8_naive(const unsigned char *data, int len);
int utf8_lookup(const unsigned char *data, int len);
int utf8_boost(const unsigned char *data, int len);
int utf8_lemire(const unsigned char *data, int len);
int utf8_range(const unsigned char *data, int len);

static struct ftab {
    const char *name;
    int (*func)(const unsigned char *data, int len);
} ftab[] = {
    {
        .name = "naive",
        .func = utf8_naive,
    }, {
        .name = "lookup",
        .func = utf8_lookup,
    }, {
        .name = "lemire",
        .func = utf8_lemire,
    }, {
        .name = "range",
        .func = utf8_range,
    },
#ifdef BOOST
    {
        .name = "boost",
        .func = utf8_boost,
    },
#endif
};

static unsigned char *load_test_file(int *len)
{
    unsigned char *data;
    int fd;
    struct stat stat;

    fd = open("./UTF-8-demo.txt", O_RDONLY);
    if (fd == -1) {
        printf("Failed to open UTF-8-demo.txt!\n");
        exit(1);
    }
    if (fstat(fd, &stat) == -1) {
        printf("Failed to get file size!\n");
        exit(1);
    }

    *len = stat.st_size;
    data = malloc(*len);
    if (read(fd, data, *len) != *len) {
        printf("Failed to read file!\n");
        exit(1);
    }

    close(fd);

    return data;
}

static void test(const unsigned char *data, int len, struct ftab *ftab)
{
    int ret;
    unsigned char save;

    ret = ftab->func(data, len);
    printf("%s(positive): %s\n", ftab->name, ret?"pass":"FAIL");

    /* Last byte can only between 00-BF */
    save = data[len-1];
    ((unsigned char *)data)[len-1] = 0xCC;
    ret = ftab->func(data, len);
    printf("%s(negative): %s\n", ftab->name, ret?"FAIL":"pass");
    ((unsigned char *)data)[len-1] = save;
}

static void bench(const unsigned char *data, int len, struct ftab *ftab)
{
    const int loops = 1024*1024*1024/len;
    int ret = 1;
    double time, size;
    struct timeval tv1, tv2;

    fprintf(stderr, "bench %s... ", ftab->name);
    gettimeofday(&tv1, 0);
    for (int i = 0; i < loops; ++i)
        ret &= ftab->func(data, len);
    gettimeofday(&tv2, 0);
    printf("%s\n", ret?"pass":"FAIL");

    time = tv2.tv_usec - tv1.tv_usec;
    time = time / 1000000 + tv2.tv_sec - tv1.tv_sec;
    size = ((double)len * loops) / (1024*1024);
    printf("time: %.4f s\n", time);
    printf("data: %.0f MB\n", size);
    printf("BW: %.2f MB/s\n", size / time);
}

static void usage(const char *bin)
{
    printf("Usage:\n");
    printf("%s test  [alg] ==> test all or one algorithm\n", bin);
    printf("%s bench [alg] ==> benchmark all or one algorithm\n", bin);
    printf("[alg] = ");
    for (int i = 0; i < sizeof(ftab)/sizeof(ftab[0]); ++i)
        printf("%s ", ftab[i].name);
    printf("\n");
}

int main(int argc, char *argv[])
{
    int len;
    unsigned char *data;
    const char * alg = NULL;
    void (*tb)(const unsigned char *data, int len, struct ftab *ftab) = NULL;

    if (argc >= 2) {
        if (strcmp(argv[1], "test") == 0)
            tb = test;
        else if (strcmp(argv[1], "bench") == 0)
            tb = bench;
        if (argc >= 3)
            alg = argv[2];
    }

    if (tb == NULL) {
        usage(argv[0]);
        return 1;
    }

    /* Load UTF8 test buffer */
    data = load_test_file(&len);

    printf("==================== UTF8 ====================\n");
    for (int i = 0; i < sizeof(ftab)/sizeof(ftab[0]); ++i) {
        if (alg && strcmp(alg, ftab[i].name) != 0)
            continue;
        tb((const unsigned char *)data, len, &ftab[i]);
        printf("\n");
    }

    /* Change test buffer to ascii */
    for (int i = 0; i < len; i++)
        data[i] &= 0x7F;

    printf("==================== ASCII ====================\n");
    for (int i = 0; i < sizeof(ftab)/sizeof(ftab[0]); ++i) {
        if (alg && strcmp(alg, ftab[i].name) != 0)
            continue;
        tb((const unsigned char *)data, len, &ftab[i]);
        printf("\n");
    }

    free(data);

    return 0;
}
