#include <jni.h>
#include <stdlib.h>
#include <string.h>
#include <unicode/unorm2.h>
#include <unicode/uchar.h>
#include <unicode/ustring.h>
#include <unicode/utypes.h>
/*
char* normalizePath(const char* path) {
    if (path == NULL) {
        return NULL;
    }

    UErrorCode status = U_ZERO_ERROR;

    // 获取 NFD 规范化器
    const UNormalizer2* normalizer = unorm2_getNFDInstance(&status);
    if (U_FAILURE(status)) {
        return strdup(path);
    }

    // 将 UTF-8 转换为 UTF-16
    int32_t pathLen = strlen(path);
    int32_t uPathCapacity = pathLen + 1;
    UChar* uPath = (UChar*)malloc(uPathCapacity * sizeof(UChar));
    if (uPath == NULL) {
        return strdup(path);
    }

    int32_t uPathLen;
    u_strFromUTF8(uPath, uPathCapacity, &uPathLen, path, pathLen, &status);

    if (status == U_BUFFER_OVERFLOW_ERROR) {
        status = U_ZERO_ERROR;
        uPathCapacity = uPathLen + 1;
        uPath = (UChar*)realloc(uPath, uPathCapacity * sizeof(UChar));
        u_strFromUTF8(uPath, uPathCapacity, &uPathLen, path, pathLen, &status);
    }

    if (U_FAILURE(status)) {
        free(uPath);
        return strdup(path);
    }

    // 执行 NFD 规范化
    int32_t normalizedCapacity = uPathLen * 2 + 1;
    UChar* normalized = (UChar*)malloc(normalizedCapacity * sizeof(UChar));
    if (normalized == NULL) {
        free(uPath);
        return strdup(path);
    }

    int32_t normalizedLen = unorm2_normalize(normalizer, uPath, uPathLen,
                                             normalized, normalizedCapacity, &status);

    if (status == U_BUFFER_OVERFLOW_ERROR) {
        status = U_ZERO_ERROR;
        normalizedCapacity = normalizedLen + 1;
        normalized = (UChar*)realloc(normalized, normalizedCapacity * sizeof(UChar));
        normalizedLen = unorm2_normalize(normalizer, uPath, uPathLen,
                                         normalized, normalizedCapacity, &status);
    }

    free(uPath);

    if (U_FAILURE(status)) {
        free(normalized);
        return strdup(path);
    }

    // 检查并移除默认可忽略的码点
    UChar* filtered = (UChar*)malloc(normalizedLen * sizeof(UChar));
    if (filtered == NULL) {
        free(normalized);
        return strdup(path);
    }

    int32_t srcIdx = 0;
    int32_t dstIdx = 0;

    while (srcIdx < normalizedLen) {
        int32_t prevIdx = srcIdx;
        UChar32 c;
        U16_NEXT(normalized, srcIdx, normalizedLen, c);

        if (!u_hasBinaryProperty(c, UCHAR_DEFAULT_IGNORABLE_CODE_POINT)) {
            // 复制该码点的 UTF-16 序列
            while (prevIdx < srcIdx) {
                filtered[dstIdx++] = normalized[prevIdx++];
            }
        }
    }

    free(normalized);
    int32_t filteredLen = dstIdx;

    // 转换为 UTF-8

    int32_t resultLen;
    u_strToUTF8(NULL, 0, &resultLen, filtered, filteredLen, &status);
    if (U_FAILURE(status)) {
        goto convert_to_utf8_err;
    }

    ++resultLen;

    char* result = (char*) malloc(resultLen);
    if (result == NULL) {
        goto convert_to_utf8_err;
    }

    u_strToUTF8(result, resultLen, NULL, filtered, filteredLen, &status);

    if (U_FAILURE(status)) {
        goto convert_to_utf8_err;
    }
    free(filtered);

    return result;

convert_to_utf8_err:
    free(filtered);
    return NULL;
}
*/