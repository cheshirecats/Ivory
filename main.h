/******************************************************
 *
 * Ivory is a high-performance HTTP + WebSocket server
 *
 * https://github.com/cheshirecats/Ivory
 *
 ******************************************************/

#include <sys/socket.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>

int server = 0;

#define echo(fmt, ...) do { if (DEBUG) printf(fmt, __VA_ARGS__); } while (0)
#define out(str) do { printf(str); } while (0)
#define die() my_exit(__LINE__)
void my_exit(int line)
{
  printf("\x1b[0;31m\nerror %d at line %d: %s\n\n\x1b[0m", errno, line, strerror(errno));
  if (server > 0) close(server);
  exit(-1);
}
void user_exit(int s)
{
  printf("\n\n");
  if (server > 0) close(server);
  exit(s);
}

typedef unsigned char BYTE;
typedef unsigned long DWORD;

struct sha1_context {
  unsigned long total[2];
  unsigned long state[5];
  BYTE buffer[64];
  BYTE ipad[64];
  BYTE opad[64];
};

#define GET_ULONG_BE(n,b,i)                             \
{                                                       \
    (n) = ( (unsigned long) (b)[(i)    ] << 24 )        \
        | ( (unsigned long) (b)[(i) + 1] << 16 )        \
        | ( (unsigned long) (b)[(i) + 2] <<  8 )        \
        | ( (unsigned long) (b)[(i) + 3]       );       \
}

#define PUT_ULONG_BE(n,b,i)                             \
{                                                       \
    (b)[(i)    ] = (BYTE) ( (n) >> 24 );       \
    (b)[(i) + 1] = (BYTE) ( (n) >> 16 );       \
    (b)[(i) + 2] = (BYTE) ( (n) >>  8 );       \
    (b)[(i) + 3] = (BYTE) ( (n)       );       \
}

void sha1_starts( sha1_context *ctx )
{
  ctx->total[0] = 0;
  ctx->total[1] = 0;

  ctx->state[0] = 0x67452301;
  ctx->state[1] = 0xEFCDAB89;
  ctx->state[2] = 0x98BADCFE;
  ctx->state[3] = 0x10325476;
  ctx->state[4] = 0xC3D2E1F0;
}

static void sha1_process( sha1_context *ctx, const BYTE data[64] )
{
  unsigned long temp, W[16], A, B, C, D, E;

  GET_ULONG_BE( W[ 0], data,  0 );
  GET_ULONG_BE( W[ 1], data,  4 );
  GET_ULONG_BE( W[ 2], data,  8 );
  GET_ULONG_BE( W[ 3], data, 12 );
  GET_ULONG_BE( W[ 4], data, 16 );
  GET_ULONG_BE( W[ 5], data, 20 );
  GET_ULONG_BE( W[ 6], data, 24 );
  GET_ULONG_BE( W[ 7], data, 28 );
  GET_ULONG_BE( W[ 8], data, 32 );
  GET_ULONG_BE( W[ 9], data, 36 );
  GET_ULONG_BE( W[10], data, 40 );
  GET_ULONG_BE( W[11], data, 44 );
  GET_ULONG_BE( W[12], data, 48 );
  GET_ULONG_BE( W[13], data, 52 );
  GET_ULONG_BE( W[14], data, 56 );
  GET_ULONG_BE( W[15], data, 60 );

#define S(x,n) ((x << n) | ((x & 0xFFFFFFFF) >> (32 - n)))

#define R(t)                                            \
(                                                       \
    temp = W[(t -  3) & 0x0F] ^ W[(t - 8) & 0x0F] ^     \
           W[(t - 14) & 0x0F] ^ W[ t      & 0x0F],      \
    ( W[t & 0x0F] = S(temp,1) )                         \
)

#define P(a,b,c,d,e,x)                                  \
{                                                       \
    e += S(a,5) + F(b,c,d) + K + x; b = S(b,30);        \
}

  A = ctx->state[0];
  B = ctx->state[1];
  C = ctx->state[2];
  D = ctx->state[3];
  E = ctx->state[4];

#define F(x,y,z) (z ^ (x & (y ^ z)))
#define K 0x5A827999

  P( A, B, C, D, E, W[0]  );
  P( E, A, B, C, D, W[1]  );
  P( D, E, A, B, C, W[2]  );
  P( C, D, E, A, B, W[3]  );
  P( B, C, D, E, A, W[4]  );
  P( A, B, C, D, E, W[5]  );
  P( E, A, B, C, D, W[6]  );
  P( D, E, A, B, C, W[7]  );
  P( C, D, E, A, B, W[8]  );
  P( B, C, D, E, A, W[9]  );
  P( A, B, C, D, E, W[10] );
  P( E, A, B, C, D, W[11] );
  P( D, E, A, B, C, W[12] );
  P( C, D, E, A, B, W[13] );
  P( B, C, D, E, A, W[14] );
  P( A, B, C, D, E, W[15] );
  P( E, A, B, C, D, R(16) );
  P( D, E, A, B, C, R(17) );
  P( C, D, E, A, B, R(18) );
  P( B, C, D, E, A, R(19) );

#undef K
#undef F

#define F(x,y,z) (x ^ y ^ z)
#define K 0x6ED9EBA1

  P( A, B, C, D, E, R(20) );
  P( E, A, B, C, D, R(21) );
  P( D, E, A, B, C, R(22) );
  P( C, D, E, A, B, R(23) );
  P( B, C, D, E, A, R(24) );
  P( A, B, C, D, E, R(25) );
  P( E, A, B, C, D, R(26) );
  P( D, E, A, B, C, R(27) );
  P( C, D, E, A, B, R(28) );
  P( B, C, D, E, A, R(29) );
  P( A, B, C, D, E, R(30) );
  P( E, A, B, C, D, R(31) );
  P( D, E, A, B, C, R(32) );
  P( C, D, E, A, B, R(33) );
  P( B, C, D, E, A, R(34) );
  P( A, B, C, D, E, R(35) );
  P( E, A, B, C, D, R(36) );
  P( D, E, A, B, C, R(37) );
  P( C, D, E, A, B, R(38) );
  P( B, C, D, E, A, R(39) );

#undef K
#undef F

#define F(x,y,z) ((x & y) | (z & (x | y)))
#define K 0x8F1BBCDC

  P( A, B, C, D, E, R(40) );
  P( E, A, B, C, D, R(41) );
  P( D, E, A, B, C, R(42) );
  P( C, D, E, A, B, R(43) );
  P( B, C, D, E, A, R(44) );
  P( A, B, C, D, E, R(45) );
  P( E, A, B, C, D, R(46) );
  P( D, E, A, B, C, R(47) );
  P( C, D, E, A, B, R(48) );
  P( B, C, D, E, A, R(49) );
  P( A, B, C, D, E, R(50) );
  P( E, A, B, C, D, R(51) );
  P( D, E, A, B, C, R(52) );
  P( C, D, E, A, B, R(53) );
  P( B, C, D, E, A, R(54) );
  P( A, B, C, D, E, R(55) );
  P( E, A, B, C, D, R(56) );
  P( D, E, A, B, C, R(57) );
  P( C, D, E, A, B, R(58) );
  P( B, C, D, E, A, R(59) );

#undef K
#undef F

#define F(x,y,z) (x ^ y ^ z)
#define K 0xCA62C1D6

  P( A, B, C, D, E, R(60) );
  P( E, A, B, C, D, R(61) );
  P( D, E, A, B, C, R(62) );
  P( C, D, E, A, B, R(63) );
  P( B, C, D, E, A, R(64) );
  P( A, B, C, D, E, R(65) );
  P( E, A, B, C, D, R(66) );
  P( D, E, A, B, C, R(67) );
  P( C, D, E, A, B, R(68) );
  P( B, C, D, E, A, R(69) );
  P( A, B, C, D, E, R(70) );
  P( E, A, B, C, D, R(71) );
  P( D, E, A, B, C, R(72) );
  P( C, D, E, A, B, R(73) );
  P( B, C, D, E, A, R(74) );
  P( A, B, C, D, E, R(75) );
  P( E, A, B, C, D, R(76) );
  P( D, E, A, B, C, R(77) );
  P( C, D, E, A, B, R(78) );
  P( B, C, D, E, A, R(79) );

#undef K
#undef F

  ctx->state[0] += A;
  ctx->state[1] += B;
  ctx->state[2] += C;
  ctx->state[3] += D;
  ctx->state[4] += E;
}

void sha1_update( sha1_context *ctx, const BYTE *input, size_t ilen )
{
  size_t fill;
  unsigned long left;

  if( ilen <= 0 )
    return;

  left = ctx->total[0] & 0x3F;
  fill = 64 - left;

  ctx->total[0] += (unsigned long) ilen;
  ctx->total[0] &= 0xFFFFFFFF;

  if( ctx->total[0] < (unsigned long) ilen )
    ctx->total[1]++;

  if( left && ilen >= fill ) {
    memcpy( (void *) (ctx->buffer + left),
            (void *) input, fill );
    sha1_process( ctx, ctx->buffer );
    input += fill;
    ilen  -= fill;
    left = 0;
  }

  while( ilen >= 64 ) {
    sha1_process( ctx, input );
    input += 64;
    ilen  -= 64;
  }

  if( ilen > 0 ) {
    memcpy( (void *) (ctx->buffer + left),
            (void *) input, ilen );
  }
}

static const BYTE sha1_padding[64] = {
  0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

void sha1_finish( sha1_context *ctx, BYTE output[20] )
{
  unsigned long last, padn;
  unsigned long high, low;
  BYTE msglen[8];

  high = ( ctx->total[0] >> 29 )
         | ( ctx->total[1] <<  3 );
  low  = ( ctx->total[0] <<  3 );

  PUT_ULONG_BE( high, msglen, 0 );
  PUT_ULONG_BE( low,  msglen, 4 );

  last = ctx->total[0] & 0x3F;
  padn = ( last < 56 ) ? ( 56 - last ) : ( 120 - last );

  sha1_update( ctx, (BYTE *) sha1_padding, padn );
  sha1_update( ctx, msglen, 8 );

  PUT_ULONG_BE( ctx->state[0], output,  0 );
  PUT_ULONG_BE( ctx->state[1], output,  4 );
  PUT_ULONG_BE( ctx->state[2], output,  8 );
  PUT_ULONG_BE( ctx->state[3], output, 12 );
  PUT_ULONG_BE( ctx->state[4], output, 16 );
}

void sha1( const BYTE *input, size_t ilen, BYTE output[20] )
{
  sha1_context ctx;

  sha1_starts( &ctx );
  sha1_update( &ctx, input, ilen );
  sha1_finish( &ctx, output );

  memset( &ctx, 0, sizeof( sha1_context ) );
}

static char encoding_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static int mod_table[] = {0, 2, 1};

void base64_encode(const BYTE *in, size_t in_length, char* out, size_t out_length)
{
  for (uint32_t i = 0, j = 0; i < in_length;) {
    uint32_t octet_a = i < in_length ? in[i++] : 0;
    uint32_t octet_b = i < in_length ? in[i++] : 0;
    uint32_t octet_c = i < in_length ? in[i++] : 0;

    uint32_t triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c;

    out[j++] = encoding_table[(triple >> 3 * 6) & 0x3F];
    out[j++] = encoding_table[(triple >> 2 * 6) & 0x3F];
    out[j++] = encoding_table[(triple >> 1 * 6) & 0x3F];
    out[j++] = encoding_table[(triple >> 0 * 6) & 0x3F];
  }

  for (int i = 0; i < mod_table[in_length % 3]; i++)
    out[out_length - 1 - i] = '=';
}

void ws_hash(const char* in, char* out)
{
  BYTE hash[20];
  char h_in[60] = {0};
  strncpy(h_in, in, 24);
  strcat(h_in, "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
  sha1((BYTE*)h_in, strlen(h_in), hash);
  base64_encode(hash, 20, out, 28);
}

char* ws_decode(char* in)
{
  int sz = (BYTE)in[1] - 128;
  for (int i = 0; i < sz; i++) {
    in[i + 6] ^= in[(i % 4) + 2];
  }
  return in + 6;
}

int ws_send(int f, const char* in, char* out)
{
  int len = strlen(in); if (len > 126) len = 126;
  out[0] = '\x81';
  out[1] = (unsigned char)len;
  strncpy(out + 2, in, len);
  out[len + 2] = 0;
  return send(f, out, len + 2, 0);
}
