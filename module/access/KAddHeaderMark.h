#ifndef KADDHEADERMARK_H
#define KADDHEADERMARK_H
#include "KMark.h"
class KAddHeaderMark: public KMark
{
public:
	KAddHeaderMark()
	{
		attr = NULL;
		val = NULL;
	}
	~KAddHeaderMark()
	{
		if(attr){
			free(attr);
		}
		if(val){
			free(val);
		}
	}
	bool mark(KHttpRequest *rq, KHttpObject *obj,
				const int chainJumpType, int &jumpType)
	{
		if(attr && val){
			assert(obj);
			obj->insertHttpHeader((const char *)attr,(const char *)val);
		}
		return true;
	}
	KMark *newInstance()
	{
		return new KAddHeaderMark;
	}
	const char *getName()
	{
		return "add_header";
	}
	std::string getHtml(KModel *model)
	{
		std::stringstream s;
		s << "attr:<input name='attr' value='";
		KAddHeaderMark *mark = (KAddHeaderMark *) (model);
		if (mark && mark->attr) {
			s << mark->attr;
		}
		s << "'>";
		s << "val:<input name=val value='";
		if (mark && mark->val) {
			s << mark->val;
		}
		s << "'>\n";
		return s.str();
	}
	std::string getDisplay()
	{
		std::stringstream s;
		if(attr){
			s << attr;
		}
		s << ": ";
		if(val){
			s << val;
		}
		return s.str();
	}
	void editHtml(std::map<std::string, std::string> &attribute) throw (KHtmlSupportException)
	{
		if(attr){
			free(attr);
			attr = NULL;
		}
		if(val){
			free(val);
			val = NULL;
		}
		if(attribute["attr"].size()>0){
			attr = strdup(attribute["attr"].c_str());
		}
		if(attribute["val"].size()>0){
			val = strdup(attribute["val"].c_str());
		}
	}
	void buildXML(std::stringstream &s)
	{
		s << " attr='" << (attr?attr:"") << "' val='" << (val?val:"") << "'>";
	}
private:
	char *attr;
	char *val;
};
#endif