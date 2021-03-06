#include <string.h>
#include <stdlib.h>
#include <vector>
#include <sstream>

#include "KHttpObjectParserHook.h"
#include "lib.h"
#include "http.h"
#include "KHttpFieldValue.h"
#include "time_utils.h"
#include "malloc_debug.h"

using namespace std;
KHttpObjectParserHook::KHttpObjectParserHook(KHttpObject *obj, KHttpRequest *rq) {
	init(obj, rq);
}
KHttpObjectParserHook::KHttpObjectParserHook() {

}
void KHttpObjectParserHook::init(KHttpObject *obj, KHttpRequest *rq) {
	this->obj = obj;
	this->rq = rq;
	serverDate = 0;
	expireDate = 0;
	keep_alive_time_out = 0;
	age = 0;

}
KHttpObjectParserHook::~KHttpObjectParserHook() {

}
void KHttpObjectParserHook::startParse() {
	responseTime = kgl_current_sec;
}
void KHttpObjectParserHook::endParse() {
	/*
	 * see rfc2616
	 * 没有 Last-Modified 我们不缓存.
	 * 但如果有 expires or max-age  除外
	 */
	if (!TEST(obj->index.flags,ANSW_LAST_MODIFIED)) {
		if (!TEST(obj->index.flags,ANSW_HAS_MAX_AGE|ANSW_HAS_EXPIRES)) {
			SET(obj->index.flags,ANSW_NO_CACHE);
		}
	}
	if (!TEST(obj->index.flags,ANSW_NO_CACHE)) {
		if (serverDate == 0) {
			serverDate = kgl_current_sec;
		}
		/*
		 * the age calculation algorithm see rfc2616 sec 13
		 */
		unsigned apparent_age = 0;
		if (responseTime > serverDate) {
			apparent_age = (unsigned) (responseTime - serverDate);
		}
		unsigned corrected_received_age = MAX(apparent_age, age);
		unsigned response_delay = (unsigned) (responseTime - rq->request_msec/1000);
		unsigned corrected_initial_age = corrected_received_age
				+ response_delay;
		unsigned resident_time = (unsigned) (kgl_current_sec - responseTime);
		age = corrected_initial_age + resident_time;
		if (!TEST(obj->index.flags,ANSW_HAS_MAX_AGE)
				&& TEST(obj->index.flags,ANSW_HAS_EXPIRES)) {
			obj->index.max_age = (unsigned) (expireDate - serverDate) - age;
		}
	}
}
int KHttpObjectParserHook::parseHeader(const char *attr, char *val,int &val_len,bool isFirst) {
	if (isFirst && proto == Proto_http) {
		if (!strncasecmp(attr, "HTTP/", 5)) {
			if (sscanf(attr + 5, "%d.%d", &httpv_major, &httpv_minor) != 2) {
				return PARSE_HEADER_FAILED;
			}
			obj->data->status_code = atoi(val);
			return PARSE_HEADER_NO_INSERT;
		} else {
			return PARSE_HEADER_FAILED;
		}
	}
	if (proto == Proto_spdy) {
		if (*attr==':') {
			attr++;
		}
	}
	if (proto != Proto_http) {
		if (!strcasecmp(attr, "Status")) {
			if (obj->data->status_code == 0) {
				obj->data->status_code = atoi(val);
			}
			return PARSE_HEADER_NO_INSERT;
		}
		if (!strcasecmp(attr, "Location")) {
			if (obj->data->status_code == 0) {
				obj->data->status_code = STATUS_FOUND;
			}
			return PARSE_HEADER_SUCCESS;
		}
		if (!strcasecmp(attr, "WWW-Authenticate")) {
			obj->data->status_code = STATUS_UNAUTH;
			return PARSE_HEADER_SUCCESS;
		}

	}
	if (!strcasecmp(attr, "Keep-Alive")) {
		char *data = strstr(val, "timeout=");
		if (data) {
			//确保有效，减掉2秒生存时间
			keep_alive_time_out = atoi(data + 8) - 2 - (int) (responseTime
					- rq->request_msec/1000);
		}
		return PARSE_HEADER_NO_INSERT;
	}
	if (!strcasecmp(attr, "Content-Range")) {
		char *p = strchr(val,'/');
		if (p) {
			rq->ctx->content_range_length = string2int(p+1);
			SET(obj->index.flags,ANSW_HAS_CONTENT_RANGE);
		}
		return PARSE_HEADER_SUCCESS;
	}
	if (!strcasecmp(attr, "Content-length")) {
		obj->index.content_length = string2int(val);
		//obj->index.content_length = atoi(val);
		SET(obj->index.flags,ANSW_HAS_CONTENT_LENGTH);
		CLR(obj->index.flags,ANSW_CHUNKED);
		return PARSE_HEADER_NO_INSERT;
	}
	if (!strcasecmp(attr,"Content-Type")) {
		if (rq->sr) {
			return PARSE_HEADER_NO_INSERT;
		}
		return PARSE_HEADER_SUCCESS;
	}
	if (!strcasecmp(attr, "Date")) {
		serverDate = parse1123time(val);
		if (rq->sr) {
			return PARSE_HEADER_NO_INSERT;
		}
		//SET(obj->index.flags,OBJ_HAS_DATE);
		return PARSE_HEADER_SUCCESS;
	}
	if (!strcasecmp(attr, "Last-Modified")) {
		obj->index.last_modified = parse1123time(val);
		if (obj->index.last_modified > 0) {
			obj->index.flags |= ANSW_LAST_MODIFIED;
		}
		return PARSE_HEADER_SUCCESS;
	}
	if (!TEST(obj->index.flags,FLAG_NEED_CACHE)) {
		if (strcasecmp(attr, "Set-Cookie") == 0) {
#ifdef ENABLE_STATIC_ENGINE
			if(!TEST(obj->index.flags,OBJ_IS_STATIC2)) {
#endif
				obj->index.flags |= ANSW_NO_CACHE;
#ifdef ENABLE_STATIC_ENGINE
			}
#endif

			return PARSE_HEADER_SUCCESS;
		}
		if (
			!strcasecmp(attr, "Pragma")) {
			if (strstr(val, "no-cache")){
				obj->index.flags |= ANSW_NO_CACHE;
			}
			return PARSE_HEADER_SUCCESS;
		}
	}
	if (!strcasecmp(attr, "Cache-Control")) {
		KHttpFieldValue field(val);
		do {
#ifdef ENABLE_STATIC_ENGINE
			if(!TEST(obj->index.flags,OBJ_IS_STATIC2)) { 
#endif
				if (field.is("no-store")) {
					obj->index.flags |= ANSW_NO_CACHE;
				} else if (field.is("no-cache")) {
					obj->index.flags |= ANSW_NO_CACHE;
				} else if (field.is("private")) {
					obj->index.flags |= ANSW_NO_CACHE;
				}
#ifdef ENABLE_STATIC_ENGINE
			}
#endif
#ifdef ENABLE_FORCE_CACHE
			if(field.is("static")){
				//通过http header强制缓存
				obj->forceCache();
			} else 
#endif
			if (field.is("public")) {
				CLR(obj->index.flags,ANSW_NO_CACHE);
			} else if (field.is("max-age=", (int *) &obj->index.max_age)) {
				obj->index.flags |= ANSW_HAS_MAX_AGE;
			} else if (field.is("s-maxage=", (int *) &obj->index.max_age)) {
				obj->index.flags |= ANSW_HAS_MAX_AGE;
			} else if (field.is("must-revalidate")) {
				obj->index.flags |= OBJ_MUST_REVALIDATE;
			}
		} while (field.next());
#ifdef ENABLE_FORCE_CACHE
		if(TEST(obj->index.flags,OBJ_IS_STATIC2)) {
			return PARSE_HEADER_NO_INSERT;
		}
#endif
		return PARSE_HEADER_SUCCESS;
	}
	
	if (!strcasecmp(attr, "Age")) {
		age = atoi(val);
		return PARSE_HEADER_NO_INSERT;
	}
	if (!strcasecmp(attr, "Connection")) {
		if (!strncasecmp(val, "keep-alive", 10))
			obj->index.flags &= ~ANSW_CLOSE;
		if (!strncasecmp(val, "close", 5))
			obj->index.flags |= ANSW_CLOSE;
		return PARSE_HEADER_NO_INSERT;
	}
	if (*attr=='x' || *attr=='X') {
		if (!TEST(rq->filter_flags,RF_NO_X_SENDFILE) &&
			(strcasecmp(attr, "X-Accel-Redirect") == 0 || strcasecmp(attr, "X-Proxy-Redirect") == 0)) {
			SET(obj->index.flags,ANSW_XSENDFILE);
			return PARSE_HEADER_INSERT_BEGIN;
		}
		if (strcasecmp(attr,"X-No-Buffer")==0) {
			SET(rq->filter_flags,RF_NO_BUFFER);
#ifdef ENABLE_TF_EXCHANGE
			rq->closeTempFile();
#endif
			return PARSE_HEADER_NO_INSERT;
		}
		if (strcasecmp(attr,"X-Gzip")==0) {
			SET(obj->index.flags,FLAG_NEED_GZIP);
			return PARSE_HEADER_NO_INSERT;
		}
	}
	if (strcasecmp(attr, "Transfer-Encoding") == 0) {
		if (strcasecmp(val, "chunked") == 0) {
			if (!TEST(obj->index.flags,ANSW_HAS_CONTENT_LENGTH)) {
				obj->index.flags |= ANSW_CHUNKED;
			}
			return PARSE_HEADER_NO_INSERT;
		}
	}
	if (strcasecmp(attr, "Content-Encoding") == 0) {
		/////////[246]
		if (strcasecmp(val, "none") == 0) {
			return PARSE_HEADER_NO_INSERT;
		}
		if (strcasecmp(val, "gzip") == 0) {
			SET(obj->index.flags,FLAG_GZIP);
			return PARSE_HEADER_NO_INSERT;
		}
		//unknow content-encoding
		//SET(obj->index.flags,FLAG_GZIP);
		return PARSE_HEADER_SUCCESS;
	}
	if (!TEST(obj->index.flags, ANSW_HAS_EXPIRES) &&
		!strcasecmp(attr,"Expires")) {
		SET(obj->index.flags,ANSW_HAS_EXPIRES);
		expireDate = parse1123time(val);
		return PARSE_HEADER_SUCCESS;
	}
/////////[247]
	return PARSE_HEADER_SUCCESS;
}
void KHttpObjectParserHook::checkHeaders(KHttpHeader *headers) {
	/*while (headers) {
	 if (strcasecmp(headers->attr, "Location")==0) {
	 KUrl m_url;
	 if (parse_url(headers->val, &m_url)) {
	 if ( (m_url.host) && (strcasecmp(obj->url->host, m_url.host)==0)
	 && (obj->url->port!=m_url.port)) {
	 stringstream s;
	 s.str("");
	 s << "http://" << m_url.host;
	 if (obj->url->port!=80) {
	 s << ":" << obj->url->port;
	 }
	 s << m_url.path;
	 free(headers->val);
	 headers->val=strdup(s.str().c_str());
	 }
	 }
	 m_url.destroy();
	 return;
	 }
	 headers = headers->next;
	 }*/
}
