# Tiny C HTTP Client

一个用纯C编写的轻量HTTP 客户端，支持 HTTP/1.1,分块传输编码,多种请求方法和自定义标头。

## Installation

```bash
git clone https://github.com/christarcher/tiny-c-http-client.git
musl-gcc test.c http.c -static -s -Os
./a.out

```
