import sys
import os
import zlib

# =============================================================================
# =============================================================================
# =============================================================================

class OsmAndCoreResourcesPacker(object):
    # -------------------------------------------------------------------------
    def __init__(self):
        return

    # -------------------------------------------------------------------------
    def pack(self, resources, outputFilename):
        # Check if output directory exists
        outputDir = os.path.dirname(outputFilename)
        if not os.path.isdir(outputDir):
            os.makedirs(outputDir)

        # Open file for writing and and write header to it
        try:
            outputFile = open(outputFilename, "w")
        except IOError:
            print("Failed to open '%s' for writing" % (outputFilename))
            return -1
        outputFile.write("// AUTOGENERATED FILE\n")
        outputFile.write("#include \"EmbeddedResources.h\"\n")
        outputFile.write("#include \"EmbeddedResources_private.h\"\n")
        outputFile.write("namespace OsmAnd {\n")

        # For each resource in collection, pack it
        for (idx, resource) in enumerate(resources):
            originalSize = os.path.getsize(resource[0])
            with open(resource[0], "rb") as resourceFile:
                resourceContent = resourceFile.read()
            
            packedContent = zlib.compress(resourceContent, 9)
            packedSize = len(packedContent)

            outputFile.write("\tstatic const QString __bundled_resource_name_%d = \"%s\";\n" % (idx, resource[1]))
            outputFile.write("\tstatic const uint8_t __bundled_resource_data_%d[] = {\n" % (idx))

            # Write size header
            outputFile.write("\t\t0x%02x, 0x%02x, 0x%02x, 0x%02x," % (
                (originalSize >> 24)&0xff,
                (originalSize >> 16)&0xff,
                (originalSize >>  8)&0xff,
                (originalSize >>  0)&0xff))

            # Write content
            for (byteIdx, byteValue) in enumerate(packedContent):
                if byteIdx % 16 == 0:
                    outputFile.write("\n\t\t")
                outputFile.write("0x%02x, " % (byteValue))
            outputFile.write("\n")
            outputFile.write("\t};\n")
            outputFile.write("\tconst size_t __bundled_resource_size_%d = 4 + %d;\n" % (idx, packedSize))

            print("Packed '%s'(%d bytes) as '%s'(4+%d bytes)..." % (resource[0], originalSize, resource[1], packedSize))
           

        # For each resource in collection, fill information about it
        outputFile.write("\tconst OsmAnd::EmbeddedResource __bundled_resources[] = {\n")
        for (idx, resource) in enumerate(resources):
            outputFile.write("\t\t{ __bundled_resource_name_%d, __bundled_resource_size_%d, &__bundled_resource_data_%d[0] },\n" % (idx, idx, idx))
        outputFile.write("\t};\n")

        # Write footer of the file and close it
        outputFile.write("\tconst uint32_t __bundled_resources_count = %d;\n" % (len(resources)))
        outputFile.write("} /* namespace OsmAnd */\n")
        outputFile.flush()
        outputFile.close()

        return 0

# =============================================================================
# =============================================================================
# =============================================================================

if __name__=='__main__':
    # Get root directory of entire project
    rootDir = os.path.realpath(os.path.join(os.path.dirname(os.path.realpath(__file__)), ".."))
    
    # Output filename
    embeddedFilename = rootDir + "/core/gen/EmbeddedResources_bundle.cpp";

    # Embedded resources
    with open(rootDir + "/core/embed-resources.list", "r") as embedResourcesListFile:
        embedResourcesList = embedResourcesListFile.readlines()
    embeddedResources = []
    for embedResourcesListEntry in embedResourcesList:
        embedResourcesListEntryParts = embedResourcesListEntry.split(':')
        originalPath = embedResourcesListEntryParts[0].strip()
        packedPath = embedResourcesListEntryParts[1].strip()
        embeddedResources.append((rootDir + originalPath, packedPath))
    
    packer = OsmAndCoreResourcesPacker()
    sys.exit(packer.pack(embeddedResources, embeddedFilename))
