#include <util/V1720InfoRawData.hxx>

namespace util {

V1720InfoRawData::V1720InfoRawData(int const bklen, int const bktype,
		const char* const name, void * const pdata) :
		TGenericData(bklen, bktype, name, pdata) {

}

const char* V1720InfoRawData::bankName() {

	return "V200";

}

InfoBank const& V1720InfoRawData::info() const {

	return *reinterpret_cast<InfoBank const*>(GetData32());

}

}