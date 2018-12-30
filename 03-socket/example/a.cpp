#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define MAX 60

int main()
{
    int K1[2][2] = {0}, K2[2][2] = {0};
    int Temp1[2] = {0}, Temp2[2] = {0};
    char P[MAX] = {0}, C[MAX] = {0};
    int T1[MAX] = {0}, T2[MAX] = {0};
    int len, flag=0, temp, temp1, i, j, num=0;


    printf("======= Hill 密码 =======\n\n");
    printf("请输入秘钥的值：\n");
    for(i=0; i<2; i++)
    {
        for(j=0; j<2; j++)
        {
            scanf("%d", &K1[i][j]);
        }
    }

    printf("\n1. 加密\t 2. 解密\n请选择：");
    scanf("%d", &num);

    if(num == 1)
    {
        printf("请输入明文：\n");
        scanf("%s", P);

        len = strlen(P);

        // 当长度为奇数时补齐一位
        if(len % 2 == 1)
        {
            P[len] = 'a';
            len = strlen(P);
            flag = 1;
        }

        // 将大写转成小写，并赋值给T1数组
        for(i=0; i<len; i++)
        {
            if(P[i] >= 'A' && P[i] <= 'Z')
            {
                P[i] = P[i] + 32;
            }

            T1[i] = P[i] - 'a';
        }


        // 得到加密后结果，存储在T2中
        for(i=0; i<len; i+=2)
        {
            Temp1[0] = T1[i];
            Temp1[1] = T1[i + 1];

            // Temp2存储密文int值
            Temp2[0] = (Temp1[0] * K1[0][0] + Temp1[1] * K1[1][0]) % 26;
            Temp2[1] = (Temp1[0] * K1[0][1] + Temp1[1] * K1[1][1]) % 26;

            T2[i] = Temp2[0];
            T2[i + 1] = Temp2[1];
        }

        if(flag == 1)
        {
            len = len - 1;
        }

        printf("加密结果为：\n");
        for(i=0; i<len; i++)
        {
            C[i] = T2[i] + 'a';
            printf("%c ", C[i]);
        }
        printf("\n");
    }

    else if(num == 2)
    {
        printf("请输入密文：");
        scanf("%s", C);

        len = strlen(C);

        // 当长度为奇数时补齐一位
        if(len % 2 == 1)
        {
            C[len] = 'a';
            len = strlen(C);
            flag = 1;
        }


        for(i=0; i<len; i++)
        {
            if(C[i] >= 'A' && C[i] <= 'Z')
            {
                C[i] = C[i] + 32;
            }

            T2[i] = C[i] - 'a';
        }

        // 求K的逆
        temp = -1;
        for(i=1; temp < 0; i++)
        {
            temp = (K1[0][0] * K1[1][1] - K1[0][1] * K1[1][0]) + 26 * i;
        }

        i = 1;
        while(1)
        {
            if((temp * i) % 26 == 1)
            {
                temp1 = i;
                break;
            }
            else
            {
                i++;
            }
        }

        K2[0][0] = K1[1][1] * temp1;
        K2[0][1] = (((-1 * K1[0][1]) + 26) * temp1) % 26;
        K2[1][0] = (((-1 * K1[1][0]) + 26) * temp1) % 26;
        K2[1][1] = K1[0][0] * temp1;

      printf(" %d %d   %d %d %d %d\n",temp, temp1, K2[0][0], K2[0][1], K2[1][0], K2[1][1]);
      system("pause");
      printf(" %d %d   %d %d %d %d\n",temp, temp1, K2[0][0]%26, K2[0][1]%26, K2[1][0]%26, K2[1][1]%26);
      system("pause");

        // 得到解密后结果，存储在T2中
        for(i=0; i<len; i+=2)
        {
            Temp2[0] = T2[i];
            Temp2[1] = T2[i + 1];

            // Temp1存储明文int值
            Temp1[0] = (Temp2[0] * K2[0][0] + Temp2[1] * K2[1][0]) % 26;
            Temp1[1] = (Temp2[0] * K2[0][1] + Temp2[1] * K2[1][1]) % 26;

            T1[i] = Temp1[0];
            T1[i + 1] = Temp1[1];
        }

        if(flag == 1)
        {
            len = len - 1;
        }

        printf("解密结果为：\n");
        for(i=0; i<len; i++)
        {
            P[i] = T1[i] + 'a';
            printf("%c ", P[i]);
        }
        printf("\n");

    }

    else
    {
        printf("error!");
        exit(0);
    }



    return 0;
}
