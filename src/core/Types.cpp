#include "core/Types.h"

QString serviceId(Service s)
{
    switch (s) {
    case Service::Auto:       return QStringLiteral("auto");
    case Service::RealDebrid: return QStringLiteral("realdebrid");
    case Service::AllDebrid:  return QStringLiteral("alldebrid");
    case Service::Direct:     return QStringLiteral("direct");
    }
    return QStringLiteral("auto");
}

Service serviceFromId(const QString& id, Service fallback)
{
    if (id == QLatin1String("auto"))       return Service::Auto;
    if (id == QLatin1String("realdebrid")) return Service::RealDebrid;
    if (id == QLatin1String("alldebrid"))  return Service::AllDebrid;
    if (id == QLatin1String("direct"))     return Service::Direct;
    return fallback;
}

QString serviceLabel(Service s)
{
    switch (s) {
    case Service::Auto:       return QStringLiteral("Auto");
    case Service::RealDebrid: return QStringLiteral("RealDebrid");
    case Service::AllDebrid:  return QStringLiteral("AllDebrid");
    case Service::Direct:     return QStringLiteral("Direct");
    }
    return {};
}
