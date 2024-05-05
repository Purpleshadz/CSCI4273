#include <openssl/md5.h>
#include<stdio.h>
#include<string.h>	//strlen
#include<stdlib.h>	//strlen

int main () {
    char balls[] = "hell0";
    
    MD5_CTX context;
    MD5_Init(&context);
    MD5_Update(&context, &balls, strlen(balls));
    unsigned char hash[16];
    MD5_Final(hash, &context);

    char hashStr[33];

    for (int i = 0; i < 16; i++) {
        sprintf(&hashStr[i*2], "%02x", (unsigned int)hash[i]);
    }
    
    printf("Output %li\n", strtol(hashStr, NULL, 16) % 4);

    return 0;
}