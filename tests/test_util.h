// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stdio.h>
#include <vicarl/error.h>

void test_fail(const char* file, int line, const char* expr);

#define ASSERT_TRUE(x) do { if(!(x)) { test_fail(__FILE__, __LINE__, #x); return; } } while(0)

#define ASSERT_EQ_U64(a,b) do { unsigned long long _a=(unsigned long long)(a), _b=(unsigned long long)(b); \
if(_a!=_b){ char buf[256]; snprintf(buf,sizeof(buf),"%s == %s (got %llu vs %llu)", #a,#b,_a,_b); test_fail(__FILE__,__LINE__,buf); return; } } while(0)

#define ASSERT_EQ_I(a,b) do { int _a=(int)(a), _b=(int)(b); \
if(_a!=_b){ char buf[256]; snprintf(buf,sizeof(buf),"%s == %s (got %d vs %d)", #a,#b,_a,_b); test_fail(__FILE__,__LINE__,buf); return; } } while(0)

#define ASSERT_ST_OK(st) do { if((st)!=VICARL_OK){ char buf[256]; snprintf(buf,sizeof(buf), "status == OK (got %d)", (int)(st)); test_fail(__FILE__,__LINE__,buf); return; } } while(0)
