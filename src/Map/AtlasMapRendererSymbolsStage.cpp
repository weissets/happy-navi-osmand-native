#include "AtlasMapRendererSymbolsStage.h"

#include "QtExtensions.h"
#include "ignore_warnings_on_external_includes.h"
#include <QLinkedList>
#include <QSet>
#include "restore_internal_warnings.h"

#include "ignore_warnings_on_external_includes.h"
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/transform.hpp>
#include "restore_internal_warnings.h"

#include "OsmAndCore.h"
#include "AtlasMapRenderer.h"
#include "AtlasMapRendererDebugStage.h"
#include "MapSymbol.h"
#include "VectorMapSymbol.h"
#include "BillboardVectorMapSymbol.h"
#include "OnSurfaceVectorMapSymbol.h"
#include "OnPathMapSymbol.h"
#include "BillboardRasterMapSymbol.h"
#include "OnSurfaceRasterMapSymbol.h"
#include "MapSymbolsGroup.h"
#include "MapSymbolsGroupWithId.h"
#include "QKeyValueIterator.h"
#include "ObjectWithId.h"

OsmAnd::AtlasMapRendererSymbolsStage::AtlasMapRendererSymbolsStage(AtlasMapRenderer* const renderer_)
    : AtlasMapRendererStage(renderer_)
{
}

OsmAnd::AtlasMapRendererSymbolsStage::~AtlasMapRendererSymbolsStage()
{
}

//#define OSMAND_KEEP_DISCARDED_SYMBOLS_IN_QUAD_TREE 1
#ifndef OSMAND_KEEP_DISCARDED_SYMBOLS_IN_QUAD_TREE
#   define OSMAND_KEEP_DISCARDED_SYMBOLS_IN_QUAD_TREE 0
#endif // !defined(OSMAND_KEEP_DISCARDED_SYMBOLS_IN_QUAD_TREE)

void OsmAnd::AtlasMapRendererSymbolsStage::obtainRenderableSymbols(
    QList< std::shared_ptr<const RenderableSymbol> >& outRenderableSymbols,
    IntersectionsQuadTree& outIntersections) const
{
    typedef QLinkedList< std::shared_ptr<const RenderableSymbol> > PlottedSymbols;
    struct PlottedSymbolRef
    {
        PlottedSymbols::iterator iterator;
        std::shared_ptr<const RenderableSymbol> renderable;
        std::shared_ptr<const MapSymbol> mapSymbol;
    };

    QReadLocker scopedLocker(&publishedMapSymbolsByOrderLock);

    // Map symbols groups sorter:
    // - If ID is available, sort by ID in ascending order
    // - If ID is unavailable, sort by pointer
    // - If ID vs no-ID is sorted, no-ID is always goes after
    const auto mapSymbolsGroupsSort =
        []
        (const std::shared_ptr<const MapSymbolsGroup>& l, const std::shared_ptr<const MapSymbolsGroup>& r) -> bool
        {
            const auto lWithId = std::dynamic_pointer_cast<const MapSymbolsGroupWithId>(l);
            const auto rWithId = std::dynamic_pointer_cast<const MapSymbolsGroupWithId>(r);

            if (lWithId && rWithId)
            {
                return lWithId->id < rWithId->id;
            }
            else if (!lWithId && !rWithId)
            {
                return l.get() < r.get();
            }
            else if (lWithId && !rWithId)
            {
                return true;
            }
            else /* if (!lWithId && rWithId) */
            {
                return false;
            }
        };

    // Iterate over map symbols layer sorted by "order" in ascending direction.
    // This means that map symbols with smaller order value are more important than map symbols with
    // larger order value.
    outIntersections = IntersectionsQuadTree(currentState.viewport, 8);
    PlottedSymbols plottedSymbols;
    QHash< const MapSymbolsGroup*, QList< PlottedSymbolRef > > plottedMapSymbolsByGroup;
    for (const auto& publishedMapSymbols : constOf(publishedMapSymbolsByOrder))
    {
        // Sort map symbols groups
        auto mapSymbolsGroups = publishedMapSymbols.keys();
        qSort(mapSymbolsGroups.begin(), mapSymbolsGroups.end(), mapSymbolsGroupsSort);

        // Iterate over all groups in proper order
        for (const auto& mapSymbolGroup : constOf(mapSymbolsGroups))
        {
            const auto citPublishedMapSymbolsFromGroup = publishedMapSymbols.constFind(mapSymbolGroup);
            if (citPublishedMapSymbolsFromGroup == publishedMapSymbols.cend())
            {
                assert(false);
                continue;
            }
            const auto& publishedMapSymbolsFromGroup = *citPublishedMapSymbolsFromGroup;

            // Process symbols from this group in order as they are stored in group
            for (const auto& mapSymbol : constOf(mapSymbolGroup->symbols))
            {
                // If this map symbol is not published yet or is located at different order, skip
                const auto citReferencesOrigins = publishedMapSymbolsFromGroup.constFind(mapSymbol);
                if (citReferencesOrigins == publishedMapSymbolsFromGroup.cend())
                    continue;
                const auto& referencesOrigins = *citReferencesOrigins;

                QList< std::shared_ptr<RenderableSymbol> > renderableSymbols;
                obtainRenderablesFromSymbol(mapSymbol, referencesOrigins, renderableSymbols);

                for (const auto& renderableSymbol : constOf(renderableSymbols))
                {
                    if (!plotSymbol(renderableSymbol, outIntersections))
                        continue;

                    const auto itPlottedSymbol = plottedSymbols.insert(plottedSymbols.end(), renderableSymbol);
                    PlottedSymbolRef plottedSymbolRef = { itPlottedSymbol, renderableSymbol, mapSymbol };

                    plottedMapSymbolsByGroup[mapSymbol->groupPtr].push_back(qMove(plottedSymbolRef));
                }
            }
        }
    }

    // Remove those plotted symbols that do not conform to presentation rules
    auto itPlottedSymbolsGroup = mutableIteratorOf(plottedMapSymbolsByGroup);
    while (itPlottedSymbolsGroup.hasNext())
    {
        auto& plottedGroupSymbols = itPlottedSymbolsGroup.next().value();

        const auto mapSymbolGroup = plottedGroupSymbols.first().mapSymbol->group.lock();
        if (!mapSymbolGroup)
        {
            // Discard entire group
            for (const auto& plottedGroupSymbol : constOf(plottedGroupSymbols))
            {
                if (Q_UNLIKELY(debugSettings->showSymbolsBBoxesRejectedByPresentationMode))
                    addRenderableDebugBox(plottedGroupSymbol.renderable, ColorARGB::fromSkColor(SK_ColorYELLOW).withAlpha(50));

#if !OSMAND_KEEP_DISCARDED_SYMBOLS_IN_QUAD_TREE
                const auto removed = outIntersections.removeOne(plottedGroupSymbol.renderable, plottedGroupSymbol.renderable->intersectionBBox);
                assert(removed);
#endif // !OSMAND_KEEP_DISCARDED_SYMBOLS_IN_QUAD_TREE
                plottedSymbols.erase(plottedGroupSymbol.iterator);
            }

            itPlottedSymbolsGroup.remove();
            continue;
        }

        // Just skip all rules
        if (mapSymbolGroup->presentationMode & MapSymbolsGroup::PresentationModeFlag::ShowAnything)
            continue;

        // Rule: show all symbols or no symbols
        if (mapSymbolGroup->presentationMode & MapSymbolsGroup::PresentationModeFlag::ShowAllOrNothing)
        {
            if (mapSymbolGroup->symbols.size() != plottedGroupSymbols.size())
            {
                // Discard entire group
                for (const auto& plottedGroupSymbol : constOf(plottedGroupSymbols))
                {
                    if (Q_UNLIKELY(debugSettings->showSymbolsBBoxesRejectedByPresentationMode))
                        addRenderableDebugBox(plottedGroupSymbol.renderable, ColorARGB::fromSkColor(SK_ColorYELLOW).withAlpha(50));

#if !OSMAND_KEEP_DISCARDED_SYMBOLS_IN_QUAD_TREE
                    const auto removed = outIntersections.removeOne(plottedGroupSymbol.renderable, plottedGroupSymbol.renderable->intersectionBBox);
                    assert(removed);
#endif // !OSMAND_KEEP_DISCARDED_SYMBOLS_IN_QUAD_TREE
                    plottedSymbols.erase(plottedGroupSymbol.iterator);
                }

                itPlottedSymbolsGroup.remove();
                continue;
            }
        }

        // Rule: if there's icon, icon must always be visible. Otherwise discard entire group
        if (mapSymbolGroup->presentationMode & MapSymbolsGroup::PresentationModeFlag::ShowNoneIfIconIsNotShown)
        {
            const auto symbolWithIconContentClass = mapSymbolGroup->getFirstSymbolWithContentClass(MapSymbol::ContentClass::Icon);
            if (symbolWithIconContentClass)
            {
                bool iconPlotted = false;
                for (const auto& plottedGroupSymbol : constOf(plottedGroupSymbols))
                {
                    if (plottedGroupSymbol.mapSymbol == symbolWithIconContentClass)
                    {
                        iconPlotted = true;
                        break;
                    }
                }

                if (!iconPlotted)
                {
                    // Discard entire group
                    for (const auto& plottedGroupSymbol : constOf(plottedGroupSymbols))
                    {
                        if (Q_UNLIKELY(debugSettings->showSymbolsBBoxesRejectedByPresentationMode))
                            addRenderableDebugBox(plottedGroupSymbol.renderable, ColorARGB::fromSkColor(SK_ColorYELLOW).withAlpha(50));

#if !OSMAND_KEEP_DISCARDED_SYMBOLS_IN_QUAD_TREE
                        const auto removed = outIntersections.removeOne(plottedGroupSymbol.renderable, plottedGroupSymbol.renderable->intersectionBBox);
                        assert(removed);
#endif // !OSMAND_KEEP_DISCARDED_SYMBOLS_IN_QUAD_TREE
                        plottedSymbols.erase(plottedGroupSymbol.iterator);
                    }

                    itPlottedSymbolsGroup.remove();
                    continue;
                }
            }
        }

        // Rule: if at least one caption was not shown, discard all other captions
        if (mapSymbolGroup->presentationMode & MapSymbolsGroup::PresentationModeFlag::ShowAllCaptionsOrNoCaptions)
        {
            const auto captionsCount = mapSymbolGroup->numberOfSymbolsWithContentClass(MapSymbol::ContentClass::Caption);
            if (captionsCount > 0)
            {
                unsigned int captionsPlotted = 0;
                for (const auto& plottedGroupSymbol : constOf(plottedGroupSymbols))
                {
                    if (plottedGroupSymbol.mapSymbol->contentClass == MapSymbol::ContentClass::Caption)
                        captionsPlotted++;
                }

                if (captionsCount != captionsPlotted)
                {
                    // Discard all plotted captions from group
                    auto itPlottedGroupSymbol = mutableIteratorOf(plottedGroupSymbols);
                    while (itPlottedGroupSymbol.hasNext())
                    {
                        const auto& plottedGroupSymbol = itPlottedGroupSymbol.next();

                        if (plottedGroupSymbol.mapSymbol->contentClass != MapSymbol::ContentClass::Caption)
                            continue;

                        if (Q_UNLIKELY(debugSettings->showSymbolsBBoxesRejectedByPresentationMode))
                            addRenderableDebugBox(plottedGroupSymbol.renderable, ColorARGB::fromSkColor(SK_ColorYELLOW).withAlpha(50));

#if !OSMAND_KEEP_DISCARDED_SYMBOLS_IN_QUAD_TREE
                        const auto removed = outIntersections.removeOne(plottedGroupSymbol.renderable, plottedGroupSymbol.renderable->intersectionBBox);
                        assert(removed);
#endif // !OSMAND_KEEP_DISCARDED_SYMBOLS_IN_QUAD_TREE
                        plottedSymbols.erase(plottedGroupSymbol.iterator);
                        itPlottedGroupSymbol.remove();
                    }
                }
            }
        }
    }

    // Publish the result
    outRenderableSymbols.clear();
    outRenderableSymbols.reserve(plottedSymbols.size());
    for (const auto& plottedSymbol : constOf(plottedSymbols))
    {
        if (Q_UNLIKELY(debugSettings->showSymbolsBBoxesAcceptedByIntersectionCheck))
            addRenderableDebugBox(plottedSymbol, ColorARGB::fromSkColor(SK_ColorGREEN).withAlpha(50));

        outRenderableSymbols.push_back(qMove(plottedSymbol));
    }
}

void OsmAnd::AtlasMapRendererSymbolsStage::obtainRenderablesFromSymbol(
    const std::shared_ptr<const MapSymbol>& mapSymbol,
    const MapRenderer::MapSymbolReferenceOrigins& referenceOrigins,
    QList< std::shared_ptr<RenderableSymbol> >& outRenderableSymbols) const
{
    if (mapSymbol->isHidden)
        return;

    if (const auto onPathMapSymbol = std::dynamic_pointer_cast<const OnPathMapSymbol>(mapSymbol))
    {
        if (Q_UNLIKELY(debugSettings->excludeOnPathSymbolsFromProcessing))
            return;

        obtainRenderablesFromOnPathSymbol(onPathMapSymbol, referenceOrigins, outRenderableSymbols);
    }
    else if (const auto onSurfaceMapSymbol = std::dynamic_pointer_cast<const IOnSurfaceMapSymbol>(mapSymbol))
    {
        if (Q_UNLIKELY(debugSettings->excludeOnSurfaceSymbolsFromProcessing))
            return;

        obtainRenderablesFromOnSurfaceSymbol(onSurfaceMapSymbol, referenceOrigins, outRenderableSymbols);
    }
    else if (const auto billboardMapSymbol = std::dynamic_pointer_cast<const IBillboardMapSymbol>(mapSymbol))
    {
        if (Q_UNLIKELY(debugSettings->excludeBillboardSymbolsFromProcessing))
            return;

        obtainRenderablesFromBillboardSymbol(billboardMapSymbol, referenceOrigins, outRenderableSymbols);
    }
    else
    {
        assert(false);
    }
}

bool OsmAnd::AtlasMapRendererSymbolsStage::plotSymbol(
    const std::shared_ptr<RenderableSymbol>& renderable,
    IntersectionsQuadTree& intersections) const
{
    if (const auto& renderableBillboard = std::dynamic_pointer_cast<RenderableBillboardSymbol>(renderable))
    {
        return plotBillboardSymbol(renderableBillboard, intersections);
    }
    else if (const auto& renderableOnPath = std::dynamic_pointer_cast<RenderableOnPathSymbol>(renderable))
    {
        return plotOnPathSymbol(renderableOnPath, intersections);
    }
    else if (const auto& renderableOnSurface = std::dynamic_pointer_cast<RenderableOnSurfaceSymbol>(renderable))
    {
        return plotOnSurfaceSymbol(renderableOnSurface, intersections);
    }

    assert(false);
    return false;
}

void OsmAnd::AtlasMapRendererSymbolsStage::obtainRenderablesFromOnPathSymbol(
    const std::shared_ptr<const OnPathMapSymbol>& onPathMapSymbol,
    const MapRenderer::MapSymbolReferenceOrigins& referenceOrigins,
    QList< std::shared_ptr<RenderableSymbol> >& outRenderableSymbols) const
{
    const auto& internalState = getInternalState();

    const auto& path31 = onPathMapSymbol->path;
    const auto pathSize = path31.size();
    const auto& symbolPinPoints = onPathMapSymbol->pinPoints;

    // Path must have at least 2 points and there must be at least one pin-point
    if (Q_UNLIKELY(pathSize < 2))
    {
        assert(false);
        return;
    }

    ////////////////////////////////////////////////////////////////////////////
    /*
    {
    if (const auto symbolsGroupWithId = std::dynamic_pointer_cast<MapSymbolsGroupWithId>(currentSymbol->group.lock()))
    {
    if ((symbolsGroupWithId->id >> 1) != 31314189)
    continue;
    }
    }
    */
    ////////////////////////////////////////////////////////////////////////////

    // Skip if there are no pin-points
    if (Q_UNLIKELY(symbolPinPoints.isEmpty()))
    {
        if (debugSettings->showTooShortOnPathSymbolsRenderablesPaths)
        {
            bool seenOnPathSymbolInGroup = false;
            bool thisIsFirstOnPathSymbolInGroup = false;
            bool allSymbolsHaveNoPinPoints = true;
            const auto symbolsGroup = onPathMapSymbol->group.lock();
            for (const auto& otherSymbol_ : constOf(symbolsGroup->symbols))
            {
                const auto otherSymbol = std::dynamic_pointer_cast<const OnPathMapSymbol>(otherSymbol_);
                if (!otherSymbol)
                    continue;

                if (!seenOnPathSymbolInGroup)
                {
                    seenOnPathSymbolInGroup = true;

                    if (otherSymbol == onPathMapSymbol)
                    {
                        thisIsFirstOnPathSymbolInGroup = true;
                        continue;
                    }
                    else
                        break;
                }

                if (!otherSymbol->pinPoints.isEmpty())
                {
                    allSymbolsHaveNoPinPoints = false;
                    break;
                }
            }

            if (thisIsFirstOnPathSymbolInGroup && allSymbolsHaveNoPinPoints)
            {
                QVector< glm::vec3 > debugPoints;
                for (const auto& pointInWorld : convertPoints31ToWorld(path31))
                {
                    debugPoints.push_back(qMove(glm::vec3(
                        pointInWorld.x,
                        0.0f,
                        pointInWorld.y)));
                }
                getRenderer()->debugStage->addLine3D(debugPoints, SK_ColorYELLOW);
            }
        }

        return;
    }

    // Get GPU resource for this map symbol, since it's useless to perform any calculations unless it's possible to draw it
    const auto gpuResource = std::dynamic_pointer_cast<const GPUAPI::TextureInGPU>(captureGpuResource(referenceOrigins, onPathMapSymbol));
    if (!gpuResource)
    {
        if (Q_UNLIKELY(debugSettings->showAllPaths))
        {
            QVector< glm::vec3 > debugPoints;
            for (const auto& pointInWorld : convertPoints31ToWorld(path31))
            {
                debugPoints.push_back(qMove(glm::vec3(
                    pointInWorld.x,
                    0.0f,
                    pointInWorld.y)));
            }
            getRenderer()->debugStage->addLine3D(debugPoints, SK_ColorCYAN);
        }

        return;
    }

    if (debugSettings->showAllPaths)
    {
        bool thisIsFirstOnPathSymbolInGroup = false;
        const auto symbolsGroup = onPathMapSymbol->group.lock();
        for (const auto& otherSymbol_ : constOf(symbolsGroup->symbols))
        {
            const auto otherSymbol = std::dynamic_pointer_cast<const OnPathMapSymbol>(otherSymbol_);
            if (!otherSymbol)
                continue;

            if (otherSymbol != onPathMapSymbol)
                break;
            thisIsFirstOnPathSymbolInGroup = true;
            break;
        }

        if (thisIsFirstOnPathSymbolInGroup)
        {
            QVector< glm::vec3 > debugPoints;
            for (const auto& pointInWorld : convertPoints31ToWorld(path31))
            {
                debugPoints.push_back(qMove(glm::vec3(
                    pointInWorld.x,
                    0.0f,
                    pointInWorld.y)));
            }
            getRenderer()->debugStage->addLine3D(debugPoints, SK_ColorGRAY);
        }
    }

    // Processing pin-points needs path in world and path on screen, as well as lengths of all segments
    const auto pathInWorld = convertPoints31ToWorld(path31);
    const auto pathSegmentsLengthsInWorld = computePathSegmentsLengths(pathInWorld);
    const auto pathOnScreen = projectFromWorldToScreen(pathInWorld);
    const auto pathSegmentsLengthsOnScreen = computePathSegmentsLengths(pathOnScreen);
    for (const auto& pinPoint : constOf(symbolPinPoints))
    {
        // Pin-point represents center of symbol
        const auto halfSizeInPixels = onPathMapSymbol->size.x / 2.0f;
        bool fits = true;
        bool is2D = true;

        // Check if this symbol instance can be rendered in 2D mode
        glm::vec2 exactStartPointOnScreen;
        glm::vec2 exactEndPointOnScreen;
        unsigned int startPathPointIndex2D = 0;
        float offsetFromStartPathPoint2D = 0.0f;
        fits = fits && computePointIndexAndOffsetFromOriginAndOffset(
            pathSegmentsLengthsOnScreen,
            pinPoint.basePathPointIndex,
            pinPoint.normalizedOffsetFromBasePathPoint,
            -halfSizeInPixels,
            startPathPointIndex2D,
            offsetFromStartPathPoint2D);
        unsigned int endPathPointIndex2D = 0;
        float offsetFromEndPathPoint2D = 0.0f;
        fits = fits && computePointIndexAndOffsetFromOriginAndOffset(
            pathSegmentsLengthsOnScreen,
            pinPoint.basePathPointIndex,
            pinPoint.normalizedOffsetFromBasePathPoint,
            halfSizeInPixels,
            endPathPointIndex2D,
            offsetFromEndPathPoint2D);
        if (fits)
        {
            exactStartPointOnScreen = computeExactPointFromOriginAndOffset(
                pathOnScreen,
                pathSegmentsLengthsOnScreen,
                startPathPointIndex2D,
                offsetFromStartPathPoint2D);
            exactEndPointOnScreen = computeExactPointFromOriginAndOffset(
                pathOnScreen,
                pathSegmentsLengthsOnScreen,
                endPathPointIndex2D,
                offsetFromEndPathPoint2D);

            is2D = pathRenderableAs2D(
                pathOnScreen,
                startPathPointIndex2D,
                exactStartPointOnScreen,
                endPathPointIndex2D,
                exactEndPointOnScreen);
        }

        // If 2D failed, check if renderable as 3D
        glm::vec2 exactStartPointInWorld;
        glm::vec2 exactEndPointInWorld;
        unsigned int startPathPointIndex3D = 0;
        float offsetFromStartPathPoint3D = 0.0f;
        unsigned int endPathPointIndex3D = 0;
        float offsetFromEndPathPoint3D = 0.0f;
        if (!fits || !is2D)
        {
            is2D = false;
            const auto halfSizeInWorld = halfSizeInPixels * internalState.pixelInWorldProjectionScale;

            fits = true;
            fits = fits && computePointIndexAndOffsetFromOriginAndOffset(
                pathSegmentsLengthsInWorld,
                pinPoint.basePathPointIndex,
                pinPoint.normalizedOffsetFromBasePathPoint,
                -halfSizeInWorld,
                startPathPointIndex3D,
                offsetFromStartPathPoint3D);
            fits = fits && computePointIndexAndOffsetFromOriginAndOffset(
                pathSegmentsLengthsInWorld,
                pinPoint.basePathPointIndex,
                pinPoint.normalizedOffsetFromBasePathPoint,
                halfSizeInWorld,
                endPathPointIndex3D,
                offsetFromEndPathPoint3D);

            if (fits)
            {
                exactStartPointInWorld = computeExactPointFromOriginAndOffset(
                    pathInWorld,
                    pathSegmentsLengthsInWorld,
                    startPathPointIndex3D,
                    offsetFromStartPathPoint3D);
                exactEndPointInWorld = computeExactPointFromOriginAndOffset(
                    pathInWorld,
                    pathSegmentsLengthsInWorld,
                    endPathPointIndex3D,
                    offsetFromEndPathPoint3D);
            }
        }

        // If this symbol instance doesn't fit in both 2D and 3D, skip it
        if (!fits)
            continue;

        // Compute exact points
        if (is2D)
        {
            // Get 3D exact points from 2D
            exactStartPointInWorld = computeExactPointFromOriginAndNormalizedOffset(
                pathInWorld,
                startPathPointIndex2D,
                offsetFromStartPathPoint2D / pathSegmentsLengthsOnScreen[startPathPointIndex2D]);
            exactEndPointInWorld = computeExactPointFromOriginAndNormalizedOffset(
                pathInWorld,
                endPathPointIndex2D,
                offsetFromEndPathPoint2D / pathSegmentsLengthsOnScreen[endPathPointIndex2D]);
        }
        else
        {
            // Get 2D exact points from 3D
            exactStartPointOnScreen = computeExactPointFromOriginAndNormalizedOffset(
                pathOnScreen,
                startPathPointIndex3D,
                offsetFromStartPathPoint3D / pathSegmentsLengthsInWorld[startPathPointIndex3D]);
            exactEndPointOnScreen = computeExactPointFromOriginAndNormalizedOffset(
                pathOnScreen,
                endPathPointIndex3D,
                offsetFromEndPathPoint3D / pathSegmentsLengthsInWorld[endPathPointIndex3D]);
        }

        // Compute direction of subpath on screen and in world
        const auto subpathStartIndex = is2D ? startPathPointIndex2D : startPathPointIndex3D;
        const auto subpathEndIndex = is2D ? endPathPointIndex2D : endPathPointIndex3D;
        assert(subpathEndIndex >= subpathStartIndex);
        const auto directionInWorld = computePathDirection(
            pathInWorld,
            subpathStartIndex,
            exactStartPointInWorld,
            subpathEndIndex,
            exactEndPointInWorld);
        const auto directionOnScreen = computePathDirection(
            pathOnScreen,
            subpathStartIndex,
            exactStartPointOnScreen,
            subpathEndIndex,
            exactEndPointOnScreen);

        // Plot symbol instance.
        std::shared_ptr<RenderableOnPathSymbol> renderable(new RenderableOnPathSymbol());
        renderable->mapSymbol = onPathMapSymbol;
        renderable->gpuResource = gpuResource;
        renderable->is2D = is2D;
        renderable->distanceToCamera = computeDistanceBetweenCameraToPath(
            pathInWorld,
            subpathStartIndex,
            exactStartPointInWorld,
            subpathEndIndex,
            exactEndPointOnScreen);
        renderable->directionInWorld = directionInWorld;
        renderable->directionOnScreen = directionOnScreen;
        renderable->glyphsPlacement = computePlacementOfGlyphsOnPath(
            is2D,
            is2D ? pathOnScreen : pathInWorld,
            is2D ? pathSegmentsLengthsOnScreen : pathSegmentsLengthsInWorld,
            subpathStartIndex,
            is2D ? offsetFromStartPathPoint2D : offsetFromStartPathPoint3D,
            subpathEndIndex,
            directionOnScreen,
            onPathMapSymbol->glyphsWidth);
        outRenderableSymbols.push_back(renderable);

        if (Q_UNLIKELY(debugSettings->showOnPathSymbolsRenderablesPaths))
        {
            const glm::vec2 directionOnScreenN(-directionOnScreen.y, directionOnScreen.x);

            // Path itself
            QVector< glm::vec3 > debugPoints;
            debugPoints.push_back(qMove(glm::vec3(
                exactStartPointInWorld.x,
                0.0f,
                exactStartPointInWorld.y)));
            auto pPointInWorld = pathInWorld.constData() + subpathStartIndex + 1;
            for (auto idx = subpathStartIndex + 1; idx <= subpathEndIndex; idx++)
            {
                const auto& pointInWorld = *(pPointInWorld++);
                debugPoints.push_back(qMove(glm::vec3(
                    pointInWorld.x,
                    0.0f,
                    pointInWorld.y)));
            }
            debugPoints.push_back(qMove(glm::vec3(
                exactEndPointInWorld.x,
                0.0f,
                exactEndPointInWorld.y)));
            getRenderer()->debugStage->addLine3D(debugPoints, is2D ? SK_ColorGREEN : SK_ColorRED);

            // Subpath N (start)
            {
                QVector<glm::vec2> lineN;
                const auto sn0 = exactStartPointOnScreen;
                lineN.push_back(glm::vec2(sn0.x, currentState.windowSize.y - sn0.y));
                const auto sn1 = sn0 + (directionOnScreenN*24.0f);
                lineN.push_back(glm::vec2(sn1.x, currentState.windowSize.y - sn1.y));
                getRenderer()->debugStage->addLine2D(lineN, SkColorSetA(SK_ColorCYAN, 128));
            }

            // Subpath N (end)
            {
                QVector<glm::vec2> lineN;
                const auto sn0 = exactEndPointOnScreen;
                lineN.push_back(glm::vec2(sn0.x, currentState.windowSize.y - sn0.y));
                const auto sn1 = sn0 + (directionOnScreenN*24.0f);
                lineN.push_back(glm::vec2(sn1.x, currentState.windowSize.y - sn1.y));
                getRenderer()->debugStage->addLine2D(lineN, SkColorSetA(SK_ColorMAGENTA, 128));
            }

            // Pin-point location
            {
                const auto pinPointInWorld = Utilities::convert31toFloat(
                    pinPoint.point31 - currentState.target31,
                    currentState.zoomBase) * static_cast<float>(AtlasMapRenderer::TileSize3D);
                const auto pinPointOnScreen = glm::project(
                    glm::vec3(pinPointInWorld.x, 0.0f, pinPointInWorld.y),
                    internalState.mCameraView,
                    internalState.mPerspectiveProjection,
                    internalState.glmViewport).xy();

                {
                    QVector<glm::vec2> lineN;
                    const auto sn0 = pinPointOnScreen;
                    lineN.push_back(glm::vec2(sn0.x, currentState.windowSize.y - sn0.y));
                    const auto sn1 = sn0 + (directionOnScreenN*32.0f);
                    lineN.push_back(glm::vec2(sn1.x, currentState.windowSize.y - sn1.y));
                    getRenderer()->debugStage->addLine2D(lineN, SkColorSetA(SK_ColorWHITE, 128));
                }

                {
                    QVector<glm::vec2> lineN;
                    const auto sn0 = pinPointOnScreen;
                    lineN.push_back(glm::vec2(sn0.x, currentState.windowSize.y - sn0.y));
                    const auto sn1 = sn0 + (directionOnScreen*32.0f);
                    lineN.push_back(glm::vec2(sn1.x, currentState.windowSize.y - sn1.y));
                    getRenderer()->debugStage->addLine2D(lineN, SkColorSetA(SK_ColorGREEN, 128));
                }
            }
        }
    }
}

QVector<glm::vec2> OsmAnd::AtlasMapRendererSymbolsStage::convertPoints31ToWorld(const QVector<PointI>& points31) const
{
    return convertPoints31ToWorld(points31, 0, points31.size() - 1);
}

QVector<glm::vec2> OsmAnd::AtlasMapRendererSymbolsStage::convertPoints31ToWorld(const QVector<PointI>& points31, unsigned int startIndex, unsigned int endIndex) const
{
    assert(endIndex >= startIndex);
    const auto count = endIndex - startIndex + 1;
    QVector<glm::vec2> result(count);
    auto pPointInWorld = result.data();
    auto pPoint31 = points31.constData() + startIndex;

    for (auto idx = 0u; idx < count; idx++)
    {
        *(pPointInWorld++) = Utilities::convert31toFloat(
                *(pPoint31++) - currentState.target31,
                currentState.zoomBase) * static_cast<float>(AtlasMapRenderer::TileSize3D);
    }

    return result;
}

QVector<glm::vec2> OsmAnd::AtlasMapRendererSymbolsStage::projectFromWorldToScreen(const QVector<glm::vec2>& pointsInWorld) const
{
    return projectFromWorldToScreen(pointsInWorld, 0, pointsInWorld.size() - 1);
}

QVector<glm::vec2> OsmAnd::AtlasMapRendererSymbolsStage::projectFromWorldToScreen(const QVector<glm::vec2>& pointsInWorld, unsigned int startIndex, unsigned int endIndex) const
{
    const auto& internalState = getInternalState();

    assert(endIndex >= startIndex);
    const auto count = endIndex - startIndex + 1;
    QVector<glm::vec2> result(count);
    auto pPointOnScreen = result.data();
    auto pPointInWorld = pointsInWorld.constData() + startIndex;

    for (auto idx = 0u; idx < count; idx++)
    {
        *(pPointOnScreen++) = glm::project(
            glm::vec3(pPointInWorld->x, 0.0f, pPointInWorld->y),
            internalState.mCameraView,
            internalState.mPerspectiveProjection,
            internalState.glmViewport).xy;
        pPointInWorld++;
    }

    return result;
}

QVector<float> OsmAnd::AtlasMapRendererSymbolsStage::computePathSegmentsLengths(const QVector<glm::vec2>& path)
{
    const auto segmentsCount = path.size() - 1;
    QVector<float> lengths(segmentsCount);

    auto pPathPoint = path.constData();
    auto pPrevPathPoint = pPathPoint++;
    auto pLength = lengths.data();
    for (auto segmentIdx = 0; segmentIdx < segmentsCount; segmentIdx++)
        *(pLength++) = glm::distance(*(pPathPoint++), *(pPrevPathPoint++));

    return lengths;
}

bool OsmAnd::AtlasMapRendererSymbolsStage::computePointIndexAndOffsetFromOriginAndOffset(
    const QVector<float>& pathSegmentsLengths,
    const unsigned int originPathPointIndex,
    const float nOffsetFromOriginPathPoint,
    const float offsetToPoint,
    unsigned int& outPathPointIndex,
    float& outOffsetFromPathPoint)
{
    const auto pathSegmentsCount = pathSegmentsLengths.size();
    const auto offsetFromOriginPathPoint = (pathSegmentsLengths[originPathPointIndex] * nOffsetFromOriginPathPoint) + offsetToPoint;

    if (offsetFromOriginPathPoint >= 0.0f)
    {
        // In case start point is located after origin point ('on the right'), usual search is used

        auto testPathPointIndex = originPathPointIndex;
        auto scannedLength = 0.0f;
        while (scannedLength < offsetFromOriginPathPoint)
        {
            if (testPathPointIndex >= pathSegmentsCount)
                return false;
            const auto& segmentLength = pathSegmentsLengths[testPathPointIndex];
            if (scannedLength + segmentLength > offsetFromOriginPathPoint)
            {
                outPathPointIndex = testPathPointIndex;
                outOffsetFromPathPoint = offsetFromOriginPathPoint - scannedLength;
                assert(outOffsetFromPathPoint >= 0.0f);
            }
            scannedLength += segmentLength;
            testPathPointIndex++;
        }
    }
    else
    {
        // In case start point is located before origin point ('on the left'), reversed search is used
        if (originPathPointIndex == 0)
            return false;

        auto testPathPointIndex = originPathPointIndex - 1;
        auto scannedLength = 0.0f;
        while (scannedLength > offsetFromOriginPathPoint)
        {
            const auto& segmentLength = pathSegmentsLengths[testPathPointIndex];
            if (scannedLength - segmentLength < offsetFromOriginPathPoint)
            {
                outPathPointIndex = testPathPointIndex;
                outOffsetFromPathPoint = segmentLength + (offsetFromOriginPathPoint - scannedLength);
                assert(outOffsetFromPathPoint >= 0.0f);
            }
            scannedLength -= segmentLength;
            if (testPathPointIndex == 0)
                return false;
            testPathPointIndex--;
        }
    }

    return true;
}

glm::vec2 OsmAnd::AtlasMapRendererSymbolsStage::computeExactPointFromOriginAndOffset(
    const QVector<glm::vec2>& path,
    const QVector<float>& pathSegmentsLengths,
    const unsigned int originPathPointIndex,
    const float offsetFromOriginPathPoint)
{
    assert(offsetFromOriginPathPoint >= 0.0f);

    const auto& originPoint = path[originPathPointIndex + 0];
    const auto& nextPoint = path[originPathPointIndex + 1];
    const auto& length = pathSegmentsLengths[originPathPointIndex];
    assert(offsetFromOriginPathPoint <= length);

    const auto exactPoint = originPoint + (nextPoint - originPoint) * (offsetFromOriginPathPoint / length);

    return exactPoint;
}

glm::vec2 OsmAnd::AtlasMapRendererSymbolsStage::computeExactPointFromOriginAndNormalizedOffset(
    const QVector<glm::vec2>& path,
    const unsigned int originPathPointIndex,
    const float nOffsetFromOriginPathPoint)
{
    assert(nOffsetFromOriginPathPoint >= 0.0f && nOffsetFromOriginPathPoint <= 1.0f);

    const auto& originPoint = path[originPathPointIndex + 0];
    const auto& nextPoint = path[originPathPointIndex + 1];

    const auto exactPoint = originPoint + (nextPoint - originPoint) * nOffsetFromOriginPathPoint;

    return exactPoint;
}

bool OsmAnd::AtlasMapRendererSymbolsStage::pathRenderableAs2D(
    const QVector<glm::vec2>& pathOnScreen,
    const unsigned int startPathPointIndex,
    const glm::vec2& exactStartPointOnScreen,
    const unsigned int endPathPointIndex,
    const glm::vec2& exactEndPointOnScreen)
{
    assert(endPathPointIndex >= startPathPointIndex);

    if (endPathPointIndex > startPathPointIndex)
    {
        const auto segmentsCount = endPathPointIndex - startPathPointIndex + 1;

        // First check segment between exact start point (which is after startPathPointIndex)
        // and next point (startPathPointIndex + 1)
        if (!segmentValidFor2D(pathOnScreen[startPathPointIndex + 1] - exactStartPointOnScreen))
            return false;

        auto pPathPoint = pathOnScreen.constData() + startPathPointIndex + 1;
        auto pPrevPathPoint = pPathPoint++;
        for (auto segmentIdx = 1; segmentIdx < segmentsCount - 1; segmentIdx++)
        {
            const auto vSegment = (*(pPathPoint++) - *(pPrevPathPoint++));
            if (!segmentValidFor2D(vSegment))
                return false;
        }

        // Last check is between pathOnScreen[endPathPointIndex] and exact end point,
        // which is always after endPathPointIndex
        assert(pPrevPathPoint == &pathOnScreen[endPathPointIndex]);
        return segmentValidFor2D(exactEndPointOnScreen - *pPrevPathPoint);
    }
    else
    {
        // In case both exact points are on the same segment, check is simple
        return segmentValidFor2D(exactEndPointOnScreen - exactStartPointOnScreen);
    }
}

bool OsmAnd::AtlasMapRendererSymbolsStage::segmentValidFor2D(const glm::vec2& vSegment)
{
    // Calculate 'incline' of line and compare to horizontal direction.
    // If any 'incline' is larger than 15 degrees, this line can not be rendered as 2D

    const static float inclineThresholdSinSq = 0.0669872981f; // qSin(qDegreesToRadians(15.0f))*qSin(qDegreesToRadians(15.0f))
    const auto d = vSegment.y;// horizont.x*vSegment.y - horizont.y*vSegment.x == 1.0f*vSegment.y - 0.0f*vSegment.x
    const auto inclineSinSq = d*d / (vSegment.x*vSegment.x + vSegment.y*vSegment.y);
    if (qAbs(inclineSinSq) > inclineThresholdSinSq)
        return false;
    return true;
}

glm::vec2 OsmAnd::AtlasMapRendererSymbolsStage::computePathDirection(
    const QVector<glm::vec2>& path,
    const unsigned int startPathPointIndex,
    const glm::vec2& exactStartPoint,
    const unsigned int endPathPointIndex,
    const glm::vec2& exactEndPoint)
{
    assert(endPathPointIndex >= startPathPointIndex);

    glm::vec2 subpathDirection;
    if (endPathPointIndex > startPathPointIndex)
    {
        const auto segmentsCount = endPathPointIndex - startPathPointIndex + 1;

        // First compute segment between exact start point (which is after startPathPointIndex)
        // and next point (startPathPointIndex + 1)
        subpathDirection += path[startPathPointIndex + 1] - exactStartPoint;
        
        auto pPathPoint = path.constData() + startPathPointIndex + 1;
        auto pPrevPathPoint = pPathPoint++;
        for (auto segmentIdx = 1; segmentIdx < segmentsCount - 1; segmentIdx++)
            subpathDirection += *(pPathPoint++) - *(pPrevPathPoint++);
        
        // Last check is between path[endPathPointIndex] and exact end point,
        // which is always after endPathPointIndex
        assert(pPrevPathPoint == &path[endPathPointIndex]);
        subpathDirection += exactEndPoint - *pPrevPathPoint;
    }
    else
    {
        // In case both exact points are on the same segment,
        // computation is simple
        subpathDirection = exactEndPoint - exactStartPoint;
    }

    return glm::normalize(subpathDirection);
}

double OsmAnd::AtlasMapRendererSymbolsStage::computeDistanceBetweenCameraToPath(
    const QVector<glm::vec2>& pathInWorld,
    const unsigned int startPathPointIndex,
    const glm::vec2& exactStartPointInWorld,
    const unsigned int endPathPointIndex,
    const glm::vec2& exactEndPointInWorld) const
{
    const auto& internalState = getInternalState();

    assert(endPathPointIndex >= startPathPointIndex);

    auto distanceToCamera = 0.0;

    // First process distance to exactStartPointInWorld
    {
        const auto distance = glm::distance(
            internalState.worldCameraPosition,
            glm::vec3(exactStartPointInWorld.x, 0.0f, exactStartPointInWorld.y));
        distanceToCamera += distance;
    }

    // Process distances to inner points
    auto pPathPointInWorld = pathInWorld.constData() + 1;
    for (auto pathPointIdx = startPathPointIndex + 1; pathPointIdx <= endPathPointIndex; pathPointIdx++)
    {
        const auto& pathPointInWorld = *(pPathPointInWorld++);

        const auto& distance = glm::distance(
            internalState.worldCameraPosition,
            glm::vec3(pathPointInWorld.x, 0.0f, pathPointInWorld.y));
        distanceToCamera += distance;
    }

    // At last process distance to exactEndPointInWorld
    {
        const auto distance = glm::distance(
            internalState.worldCameraPosition,
            glm::vec3(exactEndPointInWorld.x, 0.0f, exactEndPointInWorld.y));
        distanceToCamera += distance;
    }

    // Normalize result
    distanceToCamera /= endPathPointIndex - startPathPointIndex + 2;

    return distanceToCamera;
}

QVector<OsmAnd::AtlasMapRendererSymbolsStage::RenderableOnPathSymbol::GlyphPlacement>
OsmAnd::AtlasMapRendererSymbolsStage::computePlacementOfGlyphsOnPath(
    const bool is2D,
    const QVector<glm::vec2>& path,
    const QVector<float>& pathSegmentsLengths,
    const unsigned int startPathPointIndex,
    const float offsetFromStartPathPoint,
    const unsigned int endPathPointIndex,
    const glm::vec2& directionOnScreen,
    const QVector<float>& glyphsWidths) const
{
    const auto& internalState = getInternalState();

    assert(endPathPointIndex >= startPathPointIndex);
    const auto projectionScale = is2D ? 1.0f : internalState.pixelInWorldProjectionScale;
    const glm::vec2 directionOnScreenN(-directionOnScreen.y, directionOnScreen.x);
    const auto shouldInvert = (directionOnScreenN.y /* == horizont.x*dirN.y - horizont.y*dirN.x == 1.0f*dirN.y - 0.0f*dirN.x */) < 0;

    // Initialize glyph input and output pointers
    const auto glyphsCount = glyphsWidths.size();
    QVector<RenderableOnPathSymbol::GlyphPlacement> glyphsPlacement(glyphsCount);
    auto pGlyphPlacement = glyphsPlacement.data();
    if (shouldInvert)
    {
        // In case of direction inversion, fill from end
        pGlyphPlacement += glyphsCount - 1;
    }
    auto pGlyphWidth = glyphsWidths.constData();
    if (shouldInvert)
    {
        // In case of direction inversion, start from last glyph
        pGlyphWidth += glyphsCount - 1;
    }
    const auto glyphWidthIncrement = (shouldInvert ? -1 : +1);

    // Plot glyphs one by one
    auto segmentScanIndex = startPathPointIndex;
    auto scannedSegmentsLength = 0.0f;
    auto consumedSegmentsLength = 0.0f;
    auto prevGlyphOffset = offsetFromStartPathPoint;
    glm::vec2 currentSegmentStartPoint;
    glm::vec2 currentSegmentDirection;
    glm::vec2 currentSegmentN;
    auto currentSegmentAngle = 0.0f;
    for (int glyphIdx = 0; glyphIdx < glyphsCount; glyphIdx++, pGlyphWidth += glyphWidthIncrement)
    {
        // Get current glyph anchor offset and provide offset for next glyph
        const auto& glyphWidth = *pGlyphWidth;
        const auto glyphWidthScaled = glyphWidth * projectionScale;
        const auto anchorOffset = prevGlyphOffset + glyphWidthScaled / 2.0f;
        prevGlyphOffset += glyphWidthScaled;

        // Find path segment where this glyph should be placed
        while (anchorOffset > scannedSegmentsLength)
        {
            if (segmentScanIndex > endPathPointIndex)
            {
                // Wow! This shouldn't happen ever, since it means that glyphs doesn't fit into the provided path!
                // And this means that path calculation above gave error!
                assert(false);
                glyphsPlacement.clear();
                return glyphsPlacement;
            }

            // Check this segment
            const auto& segmentLength = pathSegmentsLengths[segmentScanIndex];
            consumedSegmentsLength = scannedSegmentsLength;
            scannedSegmentsLength += segmentLength;
            segmentScanIndex++;
            if (anchorOffset > scannedSegmentsLength)
                continue;

            // Get points for this segment
            const auto& segmentStartPoint = path[segmentScanIndex - 1 ];
            const auto& segmentEndPoint = path[segmentScanIndex - 0];
            currentSegmentStartPoint = segmentStartPoint;

            // Get segment direction and normal
            currentSegmentDirection = (segmentEndPoint - segmentStartPoint) / segmentLength;
            if (is2D)
            {
                // CCW 90 degrees rotation of Y is up
                currentSegmentN.x = -currentSegmentDirection.y;
                currentSegmentN.y = currentSegmentDirection.x;
            }
            else
            {
                // CCW 90 degrees rotation of Y is down
                currentSegmentN.x = currentSegmentDirection.y;
                currentSegmentN.y = -currentSegmentDirection.x;
            }
            currentSegmentAngle = qAtan2(currentSegmentDirection.y, currentSegmentDirection.x);//TODO: maybe for 3D a -y should be passed (see -1 rotation axis)
            if (shouldInvert)
                currentSegmentAngle = Utilities::normalizedAngleRadians(currentSegmentAngle + M_PI);
        }

        // Compute anchor point
        const auto anchorOffsetFromSegmentStartPoint = (anchorOffset - consumedSegmentsLength);
        const auto anchorPoint = currentSegmentStartPoint + anchorOffsetFromSegmentStartPoint * currentSegmentDirection;

        // Add glyph location data.
        // In case inverted, filling is performed from back-to-front. Otherwise from front-to-back
        (shouldInvert ? *(pGlyphPlacement--) : *(pGlyphPlacement++)) = RenderableOnPathSymbol::GlyphPlacement(
            anchorPoint,
            glyphWidth,
            currentSegmentAngle,
            currentSegmentN);
    }

    return glyphsPlacement;
}

void OsmAnd::AtlasMapRendererSymbolsStage::obtainRenderablesFromBillboardSymbol(
    const std::shared_ptr<const IBillboardMapSymbol>& billboardMapSymbol,
    const MapRenderer::MapSymbolReferenceOrigins& referenceOrigins,
    QList< std::shared_ptr<RenderableSymbol> >& outRenderableSymbols) const
{
    const auto& internalState = getInternalState();
    const auto mapSymbol = std::dynamic_pointer_cast<const MapSymbol>(billboardMapSymbol);

    // Get GPU resource
    const auto gpuResource = captureGpuResource(referenceOrigins, mapSymbol);
    if (!gpuResource)
        return;

    std::shared_ptr<RenderableBillboardSymbol> renderable(new RenderableBillboardSymbol());
    renderable->mapSymbol = mapSymbol;
    renderable->gpuResource = gpuResource;
    outRenderableSymbols.push_back(renderable);

    // Calculate location of symbol in world coordinates.
    renderable->offsetFromTarget31 = billboardMapSymbol->getPosition31() - currentState.target31;
    renderable->offsetFromTarget = Utilities::convert31toFloat(renderable->offsetFromTarget31, currentState.zoomBase);
    renderable->positionInWorld = glm::vec3(
        renderable->offsetFromTarget.x * AtlasMapRenderer::TileSize3D,
        0.0f,
        renderable->offsetFromTarget.y * AtlasMapRenderer::TileSize3D);

    // Get distance from symbol to camera
    renderable->distanceToCamera = glm::distance(internalState.worldCameraPosition, renderable->positionInWorld);
}

void OsmAnd::AtlasMapRendererSymbolsStage::obtainRenderablesFromOnSurfaceSymbol(
    const std::shared_ptr<const IOnSurfaceMapSymbol>& onSurfaceMapSymbol,
    const MapRenderer::MapSymbolReferenceOrigins& referenceOrigins,
    QList< std::shared_ptr<RenderableSymbol> >& outRenderableSymbols) const
{
    const auto& internalState = getInternalState();
    const auto mapSymbol = std::dynamic_pointer_cast<const MapSymbol>(onSurfaceMapSymbol);

    // Get GPU resource
    const auto gpuResource = captureGpuResource(referenceOrigins, mapSymbol);
    if (!gpuResource)
        return;

    std::shared_ptr<RenderableOnSurfaceSymbol> renderable(new RenderableOnSurfaceSymbol());
    renderable->mapSymbol = mapSymbol;
    renderable->gpuResource = gpuResource;
    outRenderableSymbols.push_back(renderable);

    // Calculate location of symbol in world coordinates.
    renderable->offsetFromTarget31 = onSurfaceMapSymbol->getPosition31() - currentState.target31;
    renderable->offsetFromTarget = Utilities::convert31toFloat(renderable->offsetFromTarget31, currentState.zoomBase);
    renderable->positionInWorld = glm::vec3(
        renderable->offsetFromTarget.x * AtlasMapRenderer::TileSize3D,
        0.0f,
        renderable->offsetFromTarget.y * AtlasMapRenderer::TileSize3D);

    // Get direction
    if (onSurfaceMapSymbol->isAzimuthAlignedDirection())
        renderable->direction = Utilities::normalizedAngleDegrees(currentState.azimuth + 180.0f);
    else
        renderable->direction = onSurfaceMapSymbol->getDirection();

    // Get distance from symbol to camera
    renderable->distanceToCamera = glm::distance(internalState.worldCameraPosition, renderable->positionInWorld);
}

bool OsmAnd::AtlasMapRendererSymbolsStage::plotBillboardSymbol(
    const std::shared_ptr<RenderableBillboardSymbol>& renderable,
    IntersectionsQuadTree& intersections) const
{
    if (std::dynamic_pointer_cast<const RasterMapSymbol>(renderable->mapSymbol))
    {
        return plotBillboardRasterSymbol(
            renderable,
            intersections);
    }
    else if (std::dynamic_pointer_cast<const VectorMapSymbol>(renderable->mapSymbol))
    {
        return plotBillboardVectorSymbol(
            renderable,
            intersections);
    }

    assert(false);
    return false;
}

bool OsmAnd::AtlasMapRendererSymbolsStage::plotBillboardRasterSymbol(
    const std::shared_ptr<RenderableBillboardSymbol>& renderable,
    IntersectionsQuadTree& intersections) const
{
    const auto& internalState = getInternalState();

    const auto& symbol = std::static_pointer_cast<const BillboardRasterMapSymbol>(renderable->mapSymbol);
    const auto& symbolGroupPtr = symbol->groupPtr;

    // Calculate position in screen coordinates (same calculation as done in shader)
    const auto symbolOnScreen = glm::project(
        renderable->positionInWorld,
        internalState.mCameraView,
        internalState.mPerspectiveProjection,
        internalState.glmViewport);

    // Get bounds in screen coordinates
    auto boundsInWindow = AreaI::fromCenterAndSize(
        static_cast<int>(symbolOnScreen.x + symbol->offset.x), static_cast<int>((currentState.windowSize.y - symbolOnScreen.y) + symbol->offset.y),
        symbol->size.x, symbol->size.y);
    renderable->intersectionBBox = boundsInWindow;
    //TODO: use symbolExtraTopSpace & symbolExtraBottomSpace from font via Rasterizer_P
//    boundsInWindow.enlargeBy(PointI(3.0f*setupOptions.displayDensityFactor, 10.0f*setupOptions.displayDensityFactor)); /* 3dip; 10dip */

    if (!applyIntersectionWithOtherSymbolsFiltering(renderable, intersections))
        return false;

    if (!applyMinDistanceToSameContentFromOtherSymbolFiltering(renderable, intersections))
        return false;

    return plotRenderable(renderable, intersections);
}

bool OsmAnd::AtlasMapRendererSymbolsStage::plotBillboardVectorSymbol(
    const std::shared_ptr<RenderableBillboardSymbol>& renderable,
    IntersectionsQuadTree& intersections) const
{
    assert(false);
    return false;
}

bool OsmAnd::AtlasMapRendererSymbolsStage::plotOnPathSymbol(
    const std::shared_ptr<RenderableOnPathSymbol>& renderable,
    IntersectionsQuadTree& intersections) const
{
    const auto& internalState = getInternalState();

    const auto& symbol = std::static_pointer_cast<const OnPathMapSymbol>(renderable->mapSymbol);
    const auto& symbolGroupPtr = symbol->groupPtr;

    // Draw the glyphs
    if (renderable->is2D)
    {
        // Calculate OOBB for 2D SOP
        const auto oobb = calculateOnPath2dOOBB(renderable);
        renderable->intersectionBBox = (OOBBI)oobb;

        //TODO: use symbolExtraTopSpace & symbolExtraBottomSpace from font via Rasterizer_P
//        oobb.enlargeBy(PointF(3.0f*setupOptions.displayDensityFactor, 10.0f*setupOptions.displayDensityFactor)); /* 3dip; 10dip */

        if (!applyIntersectionWithOtherSymbolsFiltering(renderable, intersections))
            return false;

        if (!applyMinDistanceToSameContentFromOtherSymbolFiltering(renderable, intersections))
            return false;

        if (!plotRenderable(renderable, intersections))
            return false;

        if (Q_UNLIKELY(debugSettings->showOnPath2dSymbolGlyphDetails))
        {
            for (const auto& glyph : constOf(renderable->glyphsPlacement))
            {
                getRenderer()->debugStage->addRect2D(AreaF::fromCenterAndSize(
                    glyph.anchorPoint.x, currentState.windowSize.y - glyph.anchorPoint.y,
                    glyph.width, symbol->size.y), SkColorSetA(SK_ColorGREEN, 128), glyph.angle);

                QVector<glm::vec2> lineN;
                const auto ln0 = glyph.anchorPoint;
                lineN.push_back(glm::vec2(ln0.x, currentState.windowSize.y - ln0.y));
                const auto ln1 = glyph.anchorPoint + (glyph.vNormal*16.0f);
                lineN.push_back(glm::vec2(ln1.x, currentState.windowSize.y - ln1.y));
                getRenderer()->debugStage->addLine2D(lineN, SkColorSetA(SK_ColorMAGENTA, 128));
            }
        }
    }
    else
    {
        // Calculate OOBB for 3D SOP in world
        const auto oobb = calculateOnPath3dOOBB(renderable);
        renderable->intersectionBBox = (OOBBI)oobb;

        //TODO: use symbolExtraTopSpace & symbolExtraBottomSpace from font via Rasterizer_P
//        oobb.enlargeBy(PointF(3.0f*setupOptions.displayDensityFactor, 10.0f*setupOptions.displayDensityFactor)); /* 3dip; 10dip */

        if (!applyIntersectionWithOtherSymbolsFiltering(renderable, intersections))
            return false;

        if (!applyMinDistanceToSameContentFromOtherSymbolFiltering(renderable, intersections))
            return false;

        if (!plotRenderable(renderable, intersections))
            return false;

        if (Q_UNLIKELY(debugSettings->showOnPath3dSymbolGlyphDetails))
        {
            for (const auto& glyph : constOf(renderable->glyphsPlacement))
            {
                const auto& glyphInMapPlane = AreaF::fromCenterAndSize(
                    glyph.anchorPoint.x, glyph.anchorPoint.y, /* anchor points are specified in world coordinates already */
                    glyph.width*internalState.pixelInWorldProjectionScale, symbol->size.y*internalState.pixelInWorldProjectionScale);
                const auto& tl = glyphInMapPlane.topLeft;
                const auto& tr = glyphInMapPlane.topRight();
                const auto& br = glyphInMapPlane.bottomRight;
                const auto& bl = glyphInMapPlane.bottomLeft();
                const glm::vec3 pC(glyph.anchorPoint.x, 0.0f, glyph.anchorPoint.y);
                const glm::vec4 p0(tl.x, 0.0f, tl.y, 1.0f);
                const glm::vec4 p1(tr.x, 0.0f, tr.y, 1.0f);
                const glm::vec4 p2(br.x, 0.0f, br.y, 1.0f);
                const glm::vec4 p3(bl.x, 0.0f, bl.y, 1.0f);
                const auto toCenter = glm::translate(-pC);
                const auto rotate = glm::rotate(qRadiansToDegrees((float)Utilities::normalizedAngleRadians(glyph.angle + M_PI)), glm::vec3(0.0f, -1.0f, 0.0f));
                const auto fromCenter = glm::translate(pC);
                const auto M = fromCenter*rotate*toCenter;
                getRenderer()->debugStage->addQuad3D((M*p0).xyz, (M*p1).xyz, (M*p2).xyz, (M*p3).xyz, SkColorSetA(SK_ColorGREEN, 128));

                QVector<glm::vec3> lineN;
                const auto ln0 = glyph.anchorPoint;
                lineN.push_back(glm::vec3(ln0.x, 0.0f, ln0.y));
                const auto ln1 = glyph.anchorPoint + (glyph.vNormal*16.0f*internalState.pixelInWorldProjectionScale);
                lineN.push_back(glm::vec3(ln1.x, 0.0f, ln1.y));
                getRenderer()->debugStage->addLine3D(lineN, SkColorSetA(SK_ColorMAGENTA, 128));
            }
        }
    }

    return true;
}

bool OsmAnd::AtlasMapRendererSymbolsStage::plotOnSurfaceSymbol(
    const std::shared_ptr<RenderableOnSurfaceSymbol>& renderable,
    IntersectionsQuadTree& intersections) const
{
    if (std::dynamic_pointer_cast<const RasterMapSymbol>(renderable->mapSymbol))
    {
        return plotOnSurfaceRasterSymbol(
            renderable,
            intersections);
    }
    else if (std::dynamic_pointer_cast<const VectorMapSymbol>(renderable->mapSymbol))
    {
        return plotOnSurfaceVectorSymbol(
            renderable,
            intersections);
    }

    assert(false);
    return false;
}

bool OsmAnd::AtlasMapRendererSymbolsStage::plotOnSurfaceRasterSymbol(
    const std::shared_ptr<RenderableOnSurfaceSymbol>& renderable,
    IntersectionsQuadTree& intersections) const
{
    const auto& internalState = getInternalState();

    const auto& symbol = std::static_pointer_cast<const OnSurfaceRasterMapSymbol>(renderable->mapSymbol);
    const auto& symbolGroupPtr = symbol->groupPtr;

    return true;
}

bool OsmAnd::AtlasMapRendererSymbolsStage::plotOnSurfaceVectorSymbol(
    const std::shared_ptr<RenderableOnSurfaceSymbol>& renderable,
    IntersectionsQuadTree& intersections) const
{
    const auto& internalState = getInternalState();

    const auto& symbol = std::static_pointer_cast<const OnSurfaceVectorMapSymbol>(renderable->mapSymbol);
    const auto& symbolGroupPtr = symbol->groupPtr;
    
    return true;
}

bool OsmAnd::AtlasMapRendererSymbolsStage::applyIntersectionWithOtherSymbolsFiltering(
    const std::shared_ptr<const RenderableSymbol>& renderable,
    const IntersectionsQuadTree& intersections) const
{
    const auto& symbol = renderable->mapSymbol;

    if (symbol->intersectionModeFlags & MapSymbol::IgnoredByIntersectionTest)
        return true;

    if (Q_UNLIKELY(debugSettings->skipSymbolsIntersectionCheck))
        return true;
    
    // Check intersections
    const auto checkIntersectionsInOwnGroup = !symbol->intersectionModeFlags.isSet(MapSymbol::IgnoreIntersectionsInOwnGroup);
    const auto symbolGroupPtr = symbol->groupPtr;
    const auto intersects = intersections.test(renderable->intersectionBBox, false,
        [symbolGroupPtr, checkIntersectionsInOwnGroup]
        (const std::shared_ptr<const RenderableSymbol>& otherRenderable, const IntersectionsQuadTree::BBox& otherBBox) -> bool
        {
            if (checkIntersectionsInOwnGroup)
                return true;

            // Only accept intersections with symbols from other groups
            const auto& otherSymbol = otherRenderable->mapSymbol;
            const auto shouldCheck = otherSymbol->groupPtr != symbolGroupPtr;
            return shouldCheck;
        });

    if (intersects)
    {
        if (Q_UNLIKELY(debugSettings->showSymbolsBBoxesRejectedByIntersectionCheck))
            addRenderableDebugBox(renderable, ColorARGB::fromSkColor(SK_ColorRED).withAlpha(50));
        return false;
    }
    return true;
}

bool OsmAnd::AtlasMapRendererSymbolsStage::applyMinDistanceToSameContentFromOtherSymbolFiltering(
    const std::shared_ptr<const RenderableSymbol>& renderable,
    const IntersectionsQuadTree& intersections) const
{
    const auto& symbol = std::static_pointer_cast<const RasterMapSymbol>(renderable->mapSymbol);

    if ((symbol->minDistance.x <= 0 && symbol->minDistance.y <= 0) || symbol->content.isNull())
        return true;

    if (Q_UNLIKELY(debugSettings->skipSymbolsMinDistanceToSameContentFromOtherSymbolCheck))
        return true;

    // Query for similar content in area of "minDistance" to exclude duplicates, but keep if from same mapObject
    const auto symbolGroupPtr = symbol->groupPtr;
    const auto& symbolContent = symbol->content;
    const auto hasSimilarContent = intersections.test(renderable->intersectionBBox.getEnlargedBy(symbol->minDistance), false,
        [symbolContent, symbolGroupPtr]
        (const std::shared_ptr<const RenderableSymbol>& otherRenderable, const IntersectionsQuadTree::BBox& otherBBox) -> bool
        {
            const auto otherSymbol = std::dynamic_pointer_cast<const RasterMapSymbol>(otherRenderable->mapSymbol);
            if (!otherSymbol)
                return false;

            const auto shouldCheck = (otherSymbol->content == symbolContent);
            return shouldCheck;
        });
    if (hasSimilarContent)
    {
        if (Q_UNLIKELY(debugSettings->showSymbolsBBoxesRejectedByMinDistanceToSameContentFromOtherSymbolCheck))
        {
            if (renderable->intersectionBBox.type == IntersectionsQuadTree::BBoxType::AABB)
            {
                const auto& boundsInWindow = renderable->intersectionBBox.asAABB;

                getRenderer()->debugStage->addRect2D(AreaF(boundsInWindow.getEnlargedBy(symbol->minDistance)), SkColorSetA(SK_ColorRED, 50));
                getRenderer()->debugStage->addRect2D(AreaF(boundsInWindow), SkColorSetA(SK_ColorRED, 128));
                getRenderer()->debugStage->addLine2D({
                    (glm::ivec2)boundsInWindow.topLeft,
                    (glm::ivec2)boundsInWindow.topRight(),
                    (glm::ivec2)boundsInWindow.bottomRight,
                    (glm::ivec2)boundsInWindow.bottomLeft(),
                    (glm::ivec2)boundsInWindow.topLeft
                }, SK_ColorRED);
            }
            else if (renderable->intersectionBBox.type == IntersectionsQuadTree::BBoxType::OOBB)
            {
                const auto& oobb = renderable->intersectionBBox.asOOBB;

                getRenderer()->debugStage->addRect2D((AreaF)oobb.getEnlargedBy(symbol->minDistance).unrotatedBBox(), SkColorSetA(SK_ColorRED, 50), -oobb.rotation());
                getRenderer()->debugStage->addRect2D((AreaF)oobb.unrotatedBBox(), SkColorSetA(SK_ColorRED, 128), -oobb.rotation());
                getRenderer()->debugStage->addLine2D({
                    (PointF)oobb.pointInGlobalSpace0(),
                    (PointF)oobb.pointInGlobalSpace1(),
                    (PointF)oobb.pointInGlobalSpace2(),
                    (PointF)oobb.pointInGlobalSpace3(),
                    (PointF)oobb.pointInGlobalSpace0()
                }, SK_ColorRED);
            }
        }
        return false;
    }

    return true;
}

OsmAnd::OOBBF OsmAnd::AtlasMapRendererSymbolsStage::calculateOnPath2dOOBB(const std::shared_ptr<RenderableOnPathSymbol>& renderable) const
{
    const auto& symbol = std::static_pointer_cast<const OnPathMapSymbol>(renderable->mapSymbol);

    const auto directionAngle = qAtan2(renderable->directionOnScreen.y, renderable->directionOnScreen.x);
    const auto negDirectionAngleCos = qCos(-directionAngle);
    const auto negDirectionAngleSin = qSin(-directionAngle);
    const auto directionAngleCos = qCos(directionAngle);
    const auto directionAngleSin = qSin(directionAngle);
    const auto halfGlyphHeight = symbol->size.y / 2.0f;
    auto bboxInitialized = false;
    AreaF bboxInDirection;
    for (const auto& glyph : constOf(renderable->glyphsPlacement))
    {
        const auto halfGlyphWidth = glyph.width / 2.0f;
        const glm::vec2 glyphPoints[4] =
        {
            glm::vec2(-halfGlyphWidth, -halfGlyphHeight), // TL
            glm::vec2( halfGlyphWidth, -halfGlyphHeight), // TR
            glm::vec2( halfGlyphWidth,  halfGlyphHeight), // BR
            glm::vec2(-halfGlyphWidth,  halfGlyphHeight)  // BL
        };

        const auto segmentAngleCos = qCos(glyph.angle);
        const auto segmentAngleSin = qSin(glyph.angle);

        for (int idx = 0; idx < 4; idx++)
        {
            const auto& glyphPoint = glyphPoints[idx];

            // Rotate to align with it's segment
            glm::vec2 pointOnScreen;
            pointOnScreen.x = glyphPoint.x*segmentAngleCos - glyphPoint.y*segmentAngleSin;
            pointOnScreen.y = glyphPoint.x*segmentAngleSin + glyphPoint.y*segmentAngleCos;

            // Add anchor point
            pointOnScreen += glyph.anchorPoint;

            // Rotate to align with direction
            PointF alignedPoint;
            alignedPoint.x = pointOnScreen.x*negDirectionAngleCos - pointOnScreen.y*negDirectionAngleSin;
            alignedPoint.y = pointOnScreen.x*negDirectionAngleSin + pointOnScreen.y*negDirectionAngleCos;
            if (Q_LIKELY(bboxInitialized))
                bboxInDirection.enlargeToInclude(alignedPoint);
            else
            {
                bboxInDirection.topLeft = bboxInDirection.bottomRight = alignedPoint;
                bboxInitialized = true;
            }
        }
    }
    const auto alignedCenter = bboxInDirection.center();
    bboxInDirection -= alignedCenter;
    PointF centerOnScreen;
    centerOnScreen.x = alignedCenter.x*directionAngleCos - alignedCenter.y*directionAngleSin;
    centerOnScreen.y = alignedCenter.x*directionAngleSin + alignedCenter.y*directionAngleCos;
    bboxInDirection = AreaF::fromCenterAndSize(
        centerOnScreen.x, currentState.windowSize.y - centerOnScreen.y,
        bboxInDirection.width(), bboxInDirection.height());
    
    return OOBBF(bboxInDirection, -directionAngle);
}

OsmAnd::OOBBF OsmAnd::AtlasMapRendererSymbolsStage::calculateOnPath3dOOBB(const std::shared_ptr<RenderableOnPathSymbol>& renderable) const
{
    const auto& internalState = getInternalState();
    const auto& symbol = std::static_pointer_cast<const OnPathMapSymbol>(renderable->mapSymbol);

    const auto directionAngleInWorld = qAtan2(renderable->directionInWorld.y, renderable->directionInWorld.x);
    const auto negDirectionAngleInWorldCos = qCos(-directionAngleInWorld);
    const auto negDirectionAngleInWorldSin = qSin(-directionAngleInWorld);
    const auto directionAngleInWorldCos = qCos(directionAngleInWorld);
    const auto directionAngleInWorldSin = qSin(directionAngleInWorld);
    const auto halfGlyphHeight = (symbol->size.y / 2.0f) * internalState.pixelInWorldProjectionScale;
    auto bboxInWorldInitialized = false;
    AreaF bboxInWorldDirection;
    for (const auto& glyph : constOf(renderable->glyphsPlacement))
    {
        const auto halfGlyphWidth = (glyph.width / 2.0f) * internalState.pixelInWorldProjectionScale;
        const glm::vec2 glyphPoints[4] =
        {
            glm::vec2(-halfGlyphWidth, -halfGlyphHeight), // TL
            glm::vec2( halfGlyphWidth, -halfGlyphHeight), // TR
            glm::vec2( halfGlyphWidth,  halfGlyphHeight), // BR
            glm::vec2(-halfGlyphWidth,  halfGlyphHeight)  // BL
        };

        const auto segmentAngleCos = qCos(glyph.angle);
        const auto segmentAngleSin = qSin(glyph.angle);

        for (int idx = 0; idx < 4; idx++)
        {
            const auto& glyphPoint = glyphPoints[idx];

            // Rotate to align with it's segment
            glm::vec2 pointInWorld;
            pointInWorld.x = glyphPoint.x*segmentAngleCos - glyphPoint.y*segmentAngleSin;
            pointInWorld.y = glyphPoint.x*segmentAngleSin + glyphPoint.y*segmentAngleCos;

            // Add anchor point
            pointInWorld += glyph.anchorPoint;

            // Rotate to align with direction
            PointF alignedPoint;
            alignedPoint.x = pointInWorld.x*negDirectionAngleInWorldCos - pointInWorld.y*negDirectionAngleInWorldSin;
            alignedPoint.y = pointInWorld.x*negDirectionAngleInWorldSin + pointInWorld.y*negDirectionAngleInWorldCos;
            if (Q_LIKELY(bboxInWorldInitialized))
                bboxInWorldDirection.enlargeToInclude(alignedPoint);
            else
            {
                bboxInWorldDirection.topLeft = bboxInWorldDirection.bottomRight = alignedPoint;
                bboxInWorldInitialized = true;
            }
        }
    }
    const auto alignedCenterInWorld = bboxInWorldDirection.center();
    bboxInWorldDirection -= alignedCenterInWorld;

    PointF rotatedBBoxInWorld[4];
    const auto& tl = bboxInWorldDirection.topLeft;
    rotatedBBoxInWorld[0].x = tl.x*directionAngleInWorldCos - tl.y*directionAngleInWorldSin;
    rotatedBBoxInWorld[0].y = tl.x*directionAngleInWorldSin + tl.y*directionAngleInWorldCos;
    const auto& tr = bboxInWorldDirection.topRight();
    rotatedBBoxInWorld[1].x = tr.x*directionAngleInWorldCos - tr.y*directionAngleInWorldSin;
    rotatedBBoxInWorld[1].y = tr.x*directionAngleInWorldSin + tr.y*directionAngleInWorldCos;
    const auto& br = bboxInWorldDirection.bottomRight;
    rotatedBBoxInWorld[2].x = br.x*directionAngleInWorldCos - br.y*directionAngleInWorldSin;
    rotatedBBoxInWorld[2].y = br.x*directionAngleInWorldSin + br.y*directionAngleInWorldCos;
    const auto& bl = bboxInWorldDirection.bottomLeft();
    rotatedBBoxInWorld[3].x = bl.x*directionAngleInWorldCos - bl.y*directionAngleInWorldSin;
    rotatedBBoxInWorld[3].y = bl.x*directionAngleInWorldSin + bl.y*directionAngleInWorldCos;

    PointF centerInWorld;
    centerInWorld.x = alignedCenterInWorld.x*directionAngleInWorldCos - alignedCenterInWorld.y*directionAngleInWorldSin;
    centerInWorld.y = alignedCenterInWorld.x*directionAngleInWorldSin + alignedCenterInWorld.y*directionAngleInWorldCos;
    bboxInWorldDirection += centerInWorld;
    rotatedBBoxInWorld[0] += centerInWorld;
    rotatedBBoxInWorld[1] += centerInWorld;
    rotatedBBoxInWorld[2] += centerInWorld;
    rotatedBBoxInWorld[3] += centerInWorld;

#if OSMAND_DEBUG && 0
    {
        const auto& cc = bboxInWorldDirection.center();
        const auto& tl = bboxInWorldDirection.topLeft;
        const auto& tr = bboxInWorldDirection.topRight();
        const auto& br = bboxInWorldDirection.bottomRight;
        const auto& bl = bboxInWorldDirection.bottomLeft();

        const glm::vec3 pC(cc.x, 0.0f, cc.y);
        const glm::vec4 p0(tl.x, 0.0f, tl.y, 1.0f);
        const glm::vec4 p1(tr.x, 0.0f, tr.y, 1.0f);
        const glm::vec4 p2(br.x, 0.0f, br.y, 1.0f);
        const glm::vec4 p3(bl.x, 0.0f, bl.y, 1.0f);
        const auto toCenter = glm::translate(-pC);
        const auto rotate = glm::rotate(qRadiansToDegrees((float)Utilities::normalizedAngleRadians(directionAngleInWorld + M_PI)), glm::vec3(0.0f, -1.0f, 0.0f));
        const auto fromCenter = glm::translate(pC);
        const auto M = fromCenter*rotate*toCenter;
        getRenderer()->debugStage->addQuad3D((M*p0).xyz, (M*p1).xyz, (M*p2).xyz, (M*p3).xyz, SkColorSetA(SK_ColorGREEN, 50));
    }
#endif // OSMAND_DEBUG
#if OSMAND_DEBUG && 0
        {
            const auto& tl = rotatedBBoxInWorld[0];
            const auto& tr = rotatedBBoxInWorld[1];
            const auto& br = rotatedBBoxInWorld[2];
            const auto& bl = rotatedBBoxInWorld[3];

            const glm::vec3 p0(tl.x, 0.0f, tl.y);
            const glm::vec3 p1(tr.x, 0.0f, tr.y);
            const glm::vec3 p2(br.x, 0.0f, br.y);
            const glm::vec3 p3(bl.x, 0.0f, bl.y);
            getRenderer()->debugStage->addQuad3D(p0, p1, p2, p3, SkColorSetA(SK_ColorGREEN, 50));
        }
#endif // OSMAND_DEBUG

    // Project points of OOBB in world to screen
    const PointF projectedRotatedBBoxInWorldP0(static_cast<glm::vec2>(
        glm::project(glm::vec3(rotatedBBoxInWorld[0].x, 0.0f, rotatedBBoxInWorld[0].y),
        internalState.mCameraView,
        internalState.mPerspectiveProjection,
        internalState.glmViewport).xy));
    const PointF projectedRotatedBBoxInWorldP1(static_cast<glm::vec2>(
        glm::project(glm::vec3(rotatedBBoxInWorld[1].x, 0.0f, rotatedBBoxInWorld[1].y),
        internalState.mCameraView,
        internalState.mPerspectiveProjection,
        internalState.glmViewport).xy));
    const PointF projectedRotatedBBoxInWorldP2(static_cast<glm::vec2>(
        glm::project(glm::vec3(rotatedBBoxInWorld[2].x, 0.0f, rotatedBBoxInWorld[2].y),
        internalState.mCameraView,
        internalState.mPerspectiveProjection,
        internalState.glmViewport).xy));
    const PointF projectedRotatedBBoxInWorldP3(static_cast<glm::vec2>(
        glm::project(glm::vec3(rotatedBBoxInWorld[3].x, 0.0f, rotatedBBoxInWorld[3].y),
        internalState.mCameraView,
        internalState.mPerspectiveProjection,
        internalState.glmViewport).xy));
#if OSMAND_DEBUG && 0
    {
        QVector<glm::vec2> line;
        line.push_back(glm::vec2(projectedRotatedBBoxInWorldP0.x, currentState.windowSize.y - projectedRotatedBBoxInWorldP0.y));
        line.push_back(glm::vec2(projectedRotatedBBoxInWorldP1.x, currentState.windowSize.y - projectedRotatedBBoxInWorldP1.y));
        line.push_back(glm::vec2(projectedRotatedBBoxInWorldP2.x, currentState.windowSize.y - projectedRotatedBBoxInWorldP2.y));
        line.push_back(glm::vec2(projectedRotatedBBoxInWorldP3.x, currentState.windowSize.y - projectedRotatedBBoxInWorldP3.y));
        line.push_back(glm::vec2(projectedRotatedBBoxInWorldP0.x, currentState.windowSize.y - projectedRotatedBBoxInWorldP0.y));
        getRenderer()->debugStage->addLine2D(line, SkColorSetA(SK_ColorRED, 50));
    }
#endif // OSMAND_DEBUG

    // Rotate using direction on screen
    const auto directionAngle = qAtan2(renderable->directionOnScreen.y, renderable->directionOnScreen.x);
    const auto negDirectionAngleCos = qCos(-directionAngle);
    const auto negDirectionAngleSin = qSin(-directionAngle);
    PointF bboxOnScreenP0;
    bboxOnScreenP0.x = projectedRotatedBBoxInWorldP0.x*negDirectionAngleCos - projectedRotatedBBoxInWorldP0.y*negDirectionAngleSin;
    bboxOnScreenP0.y = projectedRotatedBBoxInWorldP0.x*negDirectionAngleSin + projectedRotatedBBoxInWorldP0.y*negDirectionAngleCos;
    PointF bboxOnScreenP1;
    bboxOnScreenP1.x = projectedRotatedBBoxInWorldP1.x*negDirectionAngleCos - projectedRotatedBBoxInWorldP1.y*negDirectionAngleSin;
    bboxOnScreenP1.y = projectedRotatedBBoxInWorldP1.x*negDirectionAngleSin + projectedRotatedBBoxInWorldP1.y*negDirectionAngleCos;
    PointF bboxOnScreenP2;
    bboxOnScreenP2.x = projectedRotatedBBoxInWorldP2.x*negDirectionAngleCos - projectedRotatedBBoxInWorldP2.y*negDirectionAngleSin;
    bboxOnScreenP2.y = projectedRotatedBBoxInWorldP2.x*negDirectionAngleSin + projectedRotatedBBoxInWorldP2.y*negDirectionAngleCos;
    PointF bboxOnScreenP3;
    bboxOnScreenP3.x = projectedRotatedBBoxInWorldP3.x*negDirectionAngleCos - projectedRotatedBBoxInWorldP3.y*negDirectionAngleSin;
    bboxOnScreenP3.y = projectedRotatedBBoxInWorldP3.x*negDirectionAngleSin + projectedRotatedBBoxInWorldP3.y*negDirectionAngleCos;

    // Build bbox from that and subtract center
    AreaF bboxInDirection;
    bboxInDirection.topLeft = bboxInDirection.bottomRight = bboxOnScreenP0;
    bboxInDirection.enlargeToInclude(bboxOnScreenP1);
    bboxInDirection.enlargeToInclude(bboxOnScreenP2);
    bboxInDirection.enlargeToInclude(bboxOnScreenP3);
    const auto alignedCenter = bboxInDirection.center();
    bboxInDirection -= alignedCenter;

    // Rotate center and add it
    const auto directionAngleCos = qCos(directionAngle);
    const auto directionAngleSin = qSin(directionAngle);
    PointF centerOnScreen;
    centerOnScreen.x = alignedCenter.x*directionAngleCos - alignedCenter.y*directionAngleSin;
    centerOnScreen.y = alignedCenter.x*directionAngleSin + alignedCenter.y*directionAngleCos;
    bboxInDirection = AreaF::fromCenterAndSize(
        centerOnScreen.x, currentState.windowSize.y - centerOnScreen.y,
        bboxInDirection.width(), bboxInDirection.height());
    
    return OOBBF(bboxInDirection, -directionAngle);
}

bool OsmAnd::AtlasMapRendererSymbolsStage::plotRenderable(
    const std::shared_ptr<const RenderableSymbol>& renderable,
    IntersectionsQuadTree& intersections) const
{
    if (renderable->mapSymbol->intersectionModeFlags.isSet(MapSymbol::TransparentForIntersectionLookup) ||
        Q_UNLIKELY(debugSettings->allSymbolsTransparentForIntersectionLookup))
    {
        return true;
    }

    if (!intersections.insert(renderable, renderable->intersectionBBox))
    {
        if (Q_UNLIKELY(debugSettings->showSymbolsBBoxesRejectedByIntersectionCheck))
            addRenderableDebugBox(renderable, ColorARGB::fromSkColor(SK_ColorBLUE).withAlpha(50));

        return false;
    }

    return true;
}

std::shared_ptr<const OsmAnd::GPUAPI::ResourceInGPU> OsmAnd::AtlasMapRendererSymbolsStage::captureGpuResource(
    const MapRenderer::MapSymbolReferenceOrigins& resources,
    const std::shared_ptr<const MapSymbol>& mapSymbol)
{
    for (auto& resource : constOf(resources))
    {
        std::shared_ptr<const GPUAPI::ResourceInGPU> gpuResource;
        if (resource->setStateIf(MapRendererResourceState::Uploaded, MapRendererResourceState::IsBeingUsed))
        {
            if (const auto tiledResource = std::dynamic_pointer_cast<MapRendererTiledSymbolsResource>(resource))
                gpuResource = tiledResource->getGpuResourceFor(mapSymbol);
            else if (const auto keyedResource = std::dynamic_pointer_cast<MapRendererKeyedSymbolsResource>(resource))
                gpuResource = keyedResource->getGpuResourceFor(mapSymbol);

            resource->setState(MapRendererResourceState::Uploaded);
        }

        // Stop as soon as GPU resource found
        if (gpuResource)
            return gpuResource;
    }
    return nullptr;
}

void OsmAnd::AtlasMapRendererSymbolsStage::prepare()
{
    IntersectionsQuadTree intersections;
    obtainRenderableSymbols(renderableSymbols, intersections);

    {
        QWriteLocker scopedLocker(&_lastPreparedIntersectionsLock);
        _lastPreparedIntersections = qMove(intersections);
    }
}

void OsmAnd::AtlasMapRendererSymbolsStage::queryLastPreparedSymbolsAt(
    const PointI screenPoint,
    QList< std::shared_ptr<const MapSymbol> >& outMapSymbols) const
{
    QList< std::shared_ptr<const RenderableSymbol> > selectedRenderables;
    
    {
        QReadLocker scopedLocker(&_lastPreparedIntersectionsLock);
        _lastPreparedIntersections.select(screenPoint, selectedRenderables);
    }

    QSet< std::shared_ptr<const MapSymbol> > mapSymbolsSet;
    for (const auto& renderable : constOf(selectedRenderables))
        mapSymbolsSet.insert(renderable->mapSymbol);
    outMapSymbols = mapSymbolsSet.toList();
}

void OsmAnd::AtlasMapRendererSymbolsStage::addRenderableDebugBox(
    const std::shared_ptr<const RenderableSymbol>& renderable,
    const ColorARGB color) const
{
    if (renderable->intersectionBBox.type == IntersectionsQuadTree::BBoxType::AABB)
    {
        const auto& boundsInWindow = renderable->intersectionBBox.asAABB;

        getRenderer()->debugStage->addRect2D((AreaF)boundsInWindow, color.argb);
        getRenderer()->debugStage->addLine2D({
            (glm::ivec2)boundsInWindow.topLeft,
            (glm::ivec2)boundsInWindow.topRight(),
            (glm::ivec2)boundsInWindow.bottomRight,
            (glm::ivec2)boundsInWindow.bottomLeft(),
            (glm::ivec2)boundsInWindow.topLeft
        }, color.withAlpha(255).argb);
    }
    else /* if (renderable->intersectionBBox.type == IntersectionsQuadTree::BBoxType::OOBB) */
    {
        const auto& oobb = renderable->intersectionBBox.asOOBB;

        getRenderer()->debugStage->addRect2D((AreaF)oobb.unrotatedBBox(), color.argb, -oobb.rotation());
        getRenderer()->debugStage->addLine2D({
            (PointF)oobb.pointInGlobalSpace0(),
            (PointF)oobb.pointInGlobalSpace1(),
            (PointF)oobb.pointInGlobalSpace2(),
            (PointF)oobb.pointInGlobalSpace3(),
            (PointF)oobb.pointInGlobalSpace0(),
        }, color.withAlpha(255).argb);
    }
}

OsmAnd::AtlasMapRendererSymbolsStage::RenderableSymbol::~RenderableSymbol()
{
}

OsmAnd::AtlasMapRendererSymbolsStage::RenderableBillboardSymbol::~RenderableBillboardSymbol()
{
}

OsmAnd::AtlasMapRendererSymbolsStage::RenderableOnPathSymbol::~RenderableOnPathSymbol()
{
}

OsmAnd::AtlasMapRendererSymbolsStage::RenderableOnSurfaceSymbol::~RenderableOnSurfaceSymbol()
{
}