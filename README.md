# Tiny C HTTP Client

This is a minimal, lightweight HTTP client written in pure C that supports HTTP/1.1, chunked transfer encoding, multiple request methods, and custom headers. The client is designed to be memory-efficient and well-suited for scenarios with constrained storage, such as IoT devices.

The project is designed to be as simple and compact as possible, providing only the core functionality necessary to interact with HTTP servers. It has been tested with `scan-build` and undergone memory safety checks to ensure robustness.

## Features

- **HTTP/1.1 Support**: Fully compliant with the HTTP/1.1 specification.
- **Chunked Transfer Encoding**: Supports streaming of large responses using chunked transfer.
- **Multiple Request Methods**: Supports the following HTTP methods:
  - GET
  - POST
  - PUT
  - DELETE
  - OPTIONS
- **Custom Headers**: You can easily customize request headers, including `Host` and `Cookie`.
- **Minimalistic Design**: Written with minimal lines of code, optimized for environments with limited resources.
- **Memory Safety**: Passes memory safety checks and has been validated using tools like `scan-build`.

## Motivation

The project was originally developed to add communication capabilities to an IoT device. At the time, I found that HTTP clients written in languages like Go were too large and inefficient for embedded systems with limited memory. So, I decided to write a small, efficient HTTP client in C that would fit the requirements of such environments.

## Installation

No installation is required. Simply clone the repository and include the source files in your project.
You can use musl-gcc if you need static-linked compile

```bash
git clone https://github.com/christarcher/tiny-c-http-client.git
musl-gcc test.c http.c -static -s -Os
./a.out
