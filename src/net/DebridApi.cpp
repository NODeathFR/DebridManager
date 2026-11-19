#include "net/DebridApi.h"

#include "net/AllDebridApi.h"
#include "net/RealDebridApi.h"

DebridApi* DebridApi::create(Service s, const QString& apiKey, QObject* parent)
{
    switch (s) {
    case Service::RealDebrid: return new RealDebridApi(apiKey, parent);
    case Service::AllDebrid:  return new AllDebridApi(apiKey, parent);
    default:                  return nullptr;
    }
}
