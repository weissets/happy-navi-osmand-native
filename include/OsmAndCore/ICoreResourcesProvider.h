#ifndef _OSMAND_CORE_I_CORE_RESOURCES_PROVIDER_H_
#define _OSMAND_CORE_I_CORE_RESOURCES_PROVIDER_H_

#include <OsmAndCore/stdlib_common.h>

#include <OsmAndCore/QtExtensions.h>
#include <OsmAndCore/ignore_warnings_on_external_includes.h>
#include <QString>
#include <QByteArray>
#include <OsmAndCore/restore_internal_warnings.h>

class SkBitmap;

#include <OsmAndCore.h>
#include <OsmAndCore/Common.h>
#include <OsmAndCore/CommonSWIG.h>
#include <OsmAndCore/MemoryCommon.h>

namespace OsmAnd
{
    SWIG_DIRECTOR(ICoreResourcesProvider);
    class OSMAND_CORE_API ICoreResourcesProvider
    {
        Q_DISABLE_COPY_AND_MOVE(ICoreResourcesProvider);

    protected:
        ICoreResourcesProvider();
    public:
        virtual ~ICoreResourcesProvider();

        virtual QByteArray getResource(
            const QString& name,
            const float displayDensityFactor,
            bool* ok = nullptr) const = 0;
        virtual QByteArray getResource(
            const QString& name,
            bool* ok = nullptr) const = 0;

        virtual std::shared_ptr<SkBitmap> getResourceAsBitmap(
            const QString& name,
            const float displayDensityFactor) const;
        virtual std::shared_ptr<SkBitmap> getResourceAsBitmap(
            const QString& name) const;

        virtual bool containsResource(
            const QString& name,
            const float displayDensityFactor) const = 0;
        virtual bool containsResource(
            const QString& name) const = 0;
    };
}

#endif // !defined(_OSMAND_CORE_I_CORE_RESOURCES_PROVIDER_H_)