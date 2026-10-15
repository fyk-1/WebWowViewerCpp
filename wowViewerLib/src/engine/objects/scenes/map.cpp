//
// Created by Deamon on 7/16/2017.
//
#include <algorithm>
#include <iostream>
#include <set>
#include <cmath>
#include "map.h"
#include "../../algorithms/mathHelper.h"
#include "../../algorithms/grahamScan.h"
#include "../../persistance/wdtFile.h"
#include "../../../gapi/interface/meshes/IM2Mesh.h"
#include "../../../gapi/interface/IDevice.h"
#include "../../algorithms/quick-sort-omp.h"
#include "../../../gapi/UniformBufferStructures.h"
#include <ShaderDefinitions.h>
#include "tbb/tbb.h"
#include "../../algorithms/FrameCounter.h"

#if (__AVX__ && __SSE2__)
#include "../../algorithms/mathHelper_culling_sse.h"
#endif
#include "../../algorithms/mathHelper_culling.h"
#include "../../../gapi/interface/materials/IMaterial.h"
#include "../../../renderer/frame/FrameProfile.h"
#include "map_load_max_contants.h"
#include "../../../gapi/interface/FrameContext.h"
#include <atomic>
#include <chrono>


std::array<mathfu::vec4, 122> skyConusVBO = {
    {
       {0, 0, 0.2928932309150696f, 0},
       {0, 0.4903479814529419f, 0.13914519548416138f, 1},
       {0.12691140174865723f, 0.4736397862434387f, 0.13914519548416138f, 1},
       {0.24517399072647095f, 0.4246537685394287f, 0.13914519548416138f, 1},
       {0.3467283844947815f, 0.3467283844947815f, 0.13914519548416138f, 1},
       {0.4246538281440735f, 0.24517402052879333f, 0.13914519548416138f, 1},
       {0.4736397862434387f, 0.1269114464521408f, 0.13914519548416138f, 1},
       {0.4903479516506195f, -4.286758326088602e-8f, 0.13914519548416138f, 1},
       {0.4736397862434387f, -0.12691152095794678f, 0.13914519548416138f, 1},
       {0.4246537685394287f, -0.2451740801334381f, 0.13914519548416138f, 1},
       {0.3467283844947815f, -0.34672844409942627f, 0.13914519548416138f, 1},
       {0.24517402052879333f, -0.4246538281440735f, 0.13914519548416138f, 1},
       {0.12691132724285126f, -0.4736397862434387f, 0.13914519548416138f, 1},
       {-4.286758326088602e-8f, -0.4903479516506195f, 0.13914519548416138f, 1},
       {-0.12691141664981842f, -0.4736397862434387f, 0.13914519548416138f, 1},
       {-0.2451740801334381f, -0.42465370893478394f, 0.13914519548416138f, 1},
       {-0.34672844409942627f, -0.34672826528549194f, 0.13914519548416138f, 1},
       {-0.4246538281440735f, -0.2451738715171814f, 0.13914519548416138f, 1},
       {-0.4736397862434387f, -0.12691129744052887f, 0.13914519548416138f, 1},
       {-0.4903479516506195f, 8.573516652177204e-8f, 0.13914519548416138f, 1},
       {-0.4736397862434387f, 0.126911461353302f, 0.13914519548416138f, 1},
       {-0.4246538281440735f, 0.24517402052879333f, 0.13914519548416138f, 1},
       {-0.34672826528549194f, 0.3467285633087158f, 0.13914519548416138f, 1},
       {-0.2451738715171814f, 0.42465388774871826f, 0.13914519548416138f, 1},
       {-0.12691129744052887f, 0.4736398458480835f, 0.13914519548416138f, 1},
       {0, 0.5679999589920044f, 0.08489322662353516f, 2},
       {0.1470092087984085f, 0.5486457943916321f, 0.08489322662353516f, 2},
       {0.2839999794960022f, 0.49190235137939453f, 0.08489322662353516f, 2},
       {0.40163663029670715f, 0.40163663029670715f, 0.08489322662353516f, 2},
       {0.4919024109840393f, 0.2840000092983246f, 0.08489322662353516f, 2},
       {0.5486457943916321f, 0.1470092535018921f, 0.08489322662353516f, 2},
       {0.5679998993873596f, -4.965613342733377e-8f, 0.08489322662353516f, 2},
       {0.5486457943916321f, -0.14700935781002045f, 0.08489322662353516f, 2},
       {0.49190235137939453f, -0.28400006890296936f, 0.08489322662353516f, 2},
       {0.40163663029670715f, -0.40163668990135193f, 0.08489322662353516f, 2},
       {0.2840000092983246f, -0.4919024109840393f, 0.08489322662353516f, 2},
       {0.14700911939144135f, -0.5486457943916321f, 0.08489322662353516f, 2},
       {-4.965613342733377e-8f, -0.5679998993873596f, 0.08489322662353516f, 2},
       {-0.1470092236995697f, -0.5486457943916321f, 0.08489322662353516f, 2},
       {-0.28400006890296936f, -0.49190229177474976f, 0.08489322662353516f, 2},
       {-0.40163668990135193f, -0.4016364812850952f, 0.08489322662353516f, 2},
       {-0.4919024109840393f, -0.28399983048439026f, 0.08489322662353516f, 2},
       {-0.5486457943916321f, -0.14700908958911896f, 0.08489322662353516f, 2},
       {-0.5679998993873596f, 9.931226685466754e-8f, 0.08489322662353516f, 2},
       {-0.5486457943916321f, 0.14700926840305328f, 0.08489322662353516f, 2},
       {-0.4919024109840393f, 0.2840000092983246f, 0.08489322662353516f, 2},
       {-0.4016364812850952f, 0.4016368091106415f, 0.08489322662353516f, 2},
       {-0.28399983048439026f, 0.49190250039100647f, 0.08489322662353516f, 2},
       {-0.14700908958911896f, 0.5486459136009216f, 0.08489322662353516f, 2},
       {0, 0.6413320302963257f, 0.024161219596862793f, 3},
       {0.16598893702030182f, 0.6194791793823242f, 0.024161219596862793f, 3},
       {0.32066601514816284f, 0.5554097890853882f, 0.024161219596862793f, 3},
       {0.4534902274608612f, 0.4534902274608612f, 0.024161219596862793f, 3},
       {0.555409848690033f, 0.32066604495048523f, 0.024161219596862793f, 3},
       {0.6194791793823242f, 0.1659889966249466f, 0.024161219596862793f, 3},
       {0.6413319706916809f, -5.606702657701135e-8f, 0.024161219596862793f, 3},
       {0.6194791793823242f, -0.16598911583423615f, 0.024161219596862793f, 3},
       {0.5554097890853882f, -0.3206661343574524f, 0.024161219596862793f, 3},
       {0.4534902274608612f, -0.453490287065506f, 0.024161219596862793f, 3},
       {0.32066604495048523f, -0.555409848690033f, 0.024161219596862793f, 3},
       {0.16598884761333466f, -0.6194791793823242f, 0.024161219596862793f, 3},
       {-5.606702657701135e-8f, -0.6413319706916809f, 0.024161219596862793f, 3},
       {-0.1659889668226242f, -0.6194791793823242f, 0.024161219596862793f, 3},
       {-0.3206661343574524f, -0.5554097294807434f, 0.024161219596862793f, 3},
       {-0.453490287065506f, -0.45349007844924927f, 0.024161219596862793f, 3},
       {-0.555409848690033f, -0.3206658661365509f, 0.024161219596862793f, 3},
       {-0.6194791793823242f, -0.16598880290985107f, 0.024161219596862793f, 3},
       {-0.6413319706916809f, 1.121340531540227e-7f, 0.024161219596862793f, 3},
       {-0.6194791793823242f, 0.16598902642726898f, 0.024161219596862793f, 3},
       {-0.555409848690033f, 0.32066604495048523f, 0.024161219596862793f, 3},
       {-0.45349007844924927f, 0.4534904360771179f, 0.024161219596862793f, 3},
       {-0.3206658661365509f, 0.5554099082946777f, 0.024161219596862793f, 3},
       {-0.16598880290985107f, 0.619479238986969f, 0.024161219596862793f, 3},
       {0, 0.6530885696411133f, 0.013454735279083252f, 4},
       {0.1690317541360855f, 0.6308351159095764f, 0.013454735279083252f, 4},
       {0.32654428482055664f, 0.5655912160873413f, 0.013454735279083252f, 4},
       {0.4618033468723297f, 0.4618033468723297f, 0.013454735279083252f, 4},
       {0.5655913352966309f, 0.32654431462287903f, 0.013454735279083252f, 4},
       {0.6308351159095764f, 0.16903181374073029f, 0.013454735279083252f, 4},
       {0.6530885100364685f, -5.7094815986147296e-8f, 0.013454735279083252f, 4},
       {0.6308351159095764f, -0.16903193295001984f, 0.013454735279083252f, 4},
       {0.5655912160873413f, -0.3265444040298462f, 0.013454735279083252f, 4},
       {0.4618033468723297f, -0.4618034362792969f, 0.013454735279083252f, 4},
       {0.32654431462287903f, -0.5655913352966309f, 0.013454735279083252f, 4},
       {0.16903166472911835f, -0.6308351159095764f, 0.013454735279083252f, 4},
       {-5.7094815986147296e-8f, -0.6530885100364685f, 0.013454735279083252f, 4},
       {-0.1690317839384079f, -0.6308351159095764f, 0.013454735279083252f, 4},
       {-0.3265444040298462f, -0.5655911564826965f, 0.013454735279083252f, 4},
       {-0.4618034362792969f, -0.4618031978607178f, 0.013454735279083252f, 4},
       {-0.5655913352966309f, -0.3265441358089447f, 0.013454735279083252f, 4},
       {-0.6308351159095764f, -0.16903162002563477f, 0.013454735279083252f, 4},
       {-0.6530885100364685f, 1.1418963197229459e-7f, 0.013454735279083252f, 4},
       {-0.6308351159095764f, 0.16903184354305267f, 0.013454735279083252f, 4},
       {-0.5655913352966309f, 0.32654431462287903f, 0.013454735279083252f, 4},
       {-0.4618031978607178f, 0.4618035852909088f, 0.013454735279083252f, 4},
       {-0.3265441358089447f, 0.5655913949012756f, 0.013454735279083252f, 4},
       {-0.16903162002563477f, 0.6308351755142212f, 0.013454735279083252f, 4},
       {0, 0.6875f, -0.004999999888241291f, 5},
       {0.1779380887746811f, 0.6640740036964417f, -0.004999999888241291f, 5},
       {0.34375f, 0.5953924059867859f, -0.004999999888241291f, 5},
       {0.48613590002059937f, 0.48613590002059937f, -0.004999999888241291f, 5},
       {0.5953924655914307f, 0.3437500298023224f, -0.004999999888241291f, 5},
       {0.6640740036964417f, 0.17793814837932587f, -0.004999999888241291f, 5},
       {0.6874999403953552f, -6.010316155879991e-8f, -0.004999999888241291f, 5},
       {0.6640740036964417f, -0.1779382824897766f, -0.004999999888241291f, 5},
       {0.5953924059867859f, -0.34375011920928955f, -0.004999999888241291f, 5},
       {0.48613590002059937f, -0.48613598942756653f, -0.004999999888241291f, 5},
       {0.3437500298023224f, -0.5953924655914307f, -0.004999999888241291f, 5},
       {0.17793798446655273f, -0.6640740036964417f, -0.004999999888241291f, 5},
       {-6.010316155879991e-8f, -0.6874999403953552f, -0.004999999888241291f, 5},
       {-0.17793811857700348f, -0.6640740036964417f, -0.004999999888241291f, 5},
       {-0.34375011920928955f, -0.5953923463821411f, -0.004999999888241291f, 5},
       {-0.48613598942756653f, -0.4861357510089874f, -0.004999999888241291f, 5},
       {-0.5953924655914307f, -0.3437498211860657f, -0.004999999888241291f, 5},
       {-0.6640740036964417f, -0.17793795466423035f, -0.004999999888241291f, 5},
       {-0.6874999403953552f, 1.2020632311759982e-7f, -0.004999999888241291f, 5},
       {-0.6640740036964417f, 0.17793817818164825f, -0.004999999888241291f, 5},
       {-0.5953924655914307f, 0.3437500298023224f, -0.004999999888241291f, 5},
       {-0.4861357510089874f, 0.48613613843917847f, -0.004999999888241291f, 5},
       {-0.3437498211860657f, 0.5953925848007202f, -0.004999999888241291f, 5},
       {-0.17793795466423035f, 0.6640740633010864f, -0.004999999888241291f, 5},
       {  0,  0,  -1.7071068286895752f, 5 }
   }
};

std::array<uint16_t,300> skyConusIBO = {
    0 ,  1 ,  0  ,
    2 ,  0 ,  3  ,
    0 ,  4 ,  0  ,
    5 ,  0 ,  6  ,
    0 ,  7 ,  0  ,
    8 ,  0 ,  9  ,
    0 ,  10 ,  0  ,
    11 ,  0 ,  12  ,
    0 ,  13 ,  0  ,
    14 ,  0 ,  15  ,
    0 ,  16 ,  0  ,
    17 ,  0 ,  18  ,
    0 ,  19 ,  0  ,
    20 ,  0 ,  21  ,
    0 ,  22 ,  0  ,
    23 ,  0 ,  24  ,
    0 ,  1 ,  1  ,
    25 ,  2 ,  26  ,
    3 ,  27 ,  4  ,
    28 ,  5 ,  29  ,
    6 ,  30 ,  7  ,
    31 ,  8 ,  32  ,
    9 ,  33 ,  10  ,
    34 ,  11 ,  35  ,
    12 ,  36 ,  13  ,
    37 ,  14 ,  38  ,
    15 ,  39 ,  16  ,
    40 ,  17 ,  41  ,
    18 ,  42 ,  19  ,
    43 ,  20 ,  44  ,
    21 ,  45 ,  22  ,
    46 ,  23 ,  47  ,
    24 ,  48 ,  1  ,
    25 ,  25 ,  49  ,
    26 ,  50 ,  27  ,
    51 ,  28 ,  52  ,
    29 ,  53 ,  30  ,
    54 ,  31 ,  55  ,
    32 ,  56 ,  33  ,
    57 ,  34 ,  58  ,
    35 ,  59 ,  36  ,
    60 ,  37 ,  61  ,
    38 ,  62 ,  39  ,
    63 ,  40 ,  64  ,
    41 ,  65 ,  42  ,
    66 ,  43 ,  67  ,
    44 ,  68 ,  45  ,
    69 ,  46 ,  70  ,
    47 ,  71 ,  48  ,
    72 ,  25 ,  49  ,
    49 ,  73 ,  50  ,
    74 ,  51 ,  75  ,
    52 ,  76 ,  53  ,
    77 ,  54 ,  78  ,
    55 ,  79 ,  56  ,
    80 ,  57 ,  81  ,
    58 ,  82 ,  59  ,
    83 ,  60 ,  84  ,
    61 ,  85 ,  62  ,
    86 ,  63 ,  87  ,
    64 ,  88 ,  65  ,
    89 ,  66 ,  90  ,
    67 ,  91 ,  68  ,
    92 ,  69 ,  93  ,
    70 ,  94 ,  71  ,
    95 ,  72 ,  96  ,
    49 ,  73 ,  73  ,
    97 ,  74 ,  98  ,
    75 ,  99 ,  76  ,
    100 ,  77 ,  101  ,
    78 ,  102 ,  79  ,
    103 ,  80 ,  104  ,
    81 ,  105 ,  82  ,
    106 ,  83 ,  107  ,
    84 ,  108 ,  85  ,
    109 ,  86 ,  110  ,
    87 ,  111 ,  88  ,
    112 ,  89 ,  113  ,
    90 ,  114 ,  91  ,
    115 ,  92 ,  116  ,
    93 ,  117 ,  94  ,
    118 ,  95 ,  119  ,
    96 ,  120 ,  73  ,
    97 ,  97 ,  121  ,
    98 ,  121 ,  99  ,
    121 ,  100 ,  121  ,
    101 ,  121 ,  102  ,
    121 ,  103 ,  121  ,
    104 ,  121 ,  105  ,
    121 ,  106 ,  121  ,
    107 ,  121 ,  108  ,
    121 ,  109 ,  121  ,
    110 ,  121 ,  111  ,
    121 ,  112 ,  121  ,
    113 ,  121 ,  114  ,
    121 ,  115 ,  121  ,
    116 ,  121 ,  117  ,
    121 ,  118 ,  121  ,
    119 ,  121 ,  120  ,
    121 ,  97 ,  121  ,
};

HGVertexBufferBindings createSkyBindings(const HMapSceneBufferCreate &sceneRenderer) {
    auto skyIBO = sceneRenderer->createSkyIndexBuffer(skyConusIBO.size() * sizeof(uint16_t));
    skyIBO->uploadData(
        skyConusIBO.data(),
        skyConusIBO.size() * sizeof(uint16_t));

    auto skyVBO = sceneRenderer->createSkyVertexBuffer(skyConusVBO.size() * sizeof(mathfu::vec4_packed));
    skyVBO->uploadData(
        skyConusVBO.data(),
        skyConusVBO.size() * sizeof(mathfu::vec4_packed)
    );

    auto skyBindings = sceneRenderer->createSkyVAO(skyVBO, skyIBO);

    return skyBindings;
}

Map::Map(HApiContainer api, int mapId, const std::string &mapName) : m_dayNightLightHolder(api, mapId) {
    initialize(api, mapId);
    initMapTiles();

    this->mapName = mapName;

    std::string wdtFileName = "world/maps/" + mapName + "/" + mapName + ".wdt";
    std::string wdlFileName = "world/maps/" + mapName + "/" + mapName + ".wdl";
    std::string wdtLightFileName = "world/maps/" + mapName + "/" + mapName + "_lgt.wdt";

    m_wdtfile = api->cacheStorage->getWdtFileCache()->get(wdtFileName);
    m_wdtLightObject = std::make_shared<WdtLightsObject>(api, wdtLightFileName);

    m_wdlObject = std::make_shared<WdlObject>(api, wdlFileName);
    m_wdlObject->setMapApi(this);
}
Map::Map(HApiContainer api, int mapId, int wdtFileDataId) : m_dayNightLightHolder(api, mapId) {
    initialize(api, mapId);
    initMapTiles();

    mapName = "";
    m_wdtfile = api->cacheStorage->getWdtFileCache()->getFileId(wdtFileDataId);
}

Map::Map(HApiContainer api, std::string adtFileName, int i, int j, std::string mapName) : m_dayNightLightHolder(api, 0) {
    initialize(api, 0);

    this->mapName = mapName;

    std::string wdtFileName = "world/maps/" + mapName + "/" + mapName + ".wdt";
    std::string wdlFileName = "world/maps/" + mapName + "/" + mapName + ".wdl";

    m_wdtfile = api->cacheStorage->getWdtFileCache()->get(wdtFileName);
    m_wdlObject = std::make_shared<WdlObject>(api, wdlFileName);
    m_wdlObject->setMapApi(this);

    m_lockedMap = true;
    std::string adtFileTemplate = "world/maps/" + mapName + "/" + mapName + "_" + std::to_string(i) + "_" + std::to_string(j);
    auto adtObject = adtObjectFactory->createObject(m_api, adtFileTemplate, mapName, i, j, false, m_wdtfile);

    adtObject->setMapApi(this);
    this->mapTiles[i][j] = adtObject;
}

void Map::initialize(HApiContainer api, int mapId) {
    initMapTiles();

    m_mapId = mapId;
    m_api = api;
    m_sceneMode = SceneMode::smMap;

    createAdtFreeLamdas();

    // Only get map record and set flags if mapId is not 0
    if (mapId != 0 && api->databaseHandler != nullptr) {
        MapRecord mapRecord;
        api->databaseHandler->getMapById(mapId, mapRecord);
        useWeightedBlend = (mapRecord.flags0 & 0x4) > 0;
        has0x200000Flag = (mapRecord.flags0 & 0x200000) > 0;
    }

    m_dayNightLightHolder.loadZoneLights();

    m_sceneWideBlockVSPSChunk = nullptr;
}


std::tuple<HGMesh, std::shared_ptr<ISkyMeshMaterial>> createSkyMesh(const HMapSceneBufferCreate &sceneRenderer,
                                                              const HGVertexBufferBindings &skyBindings, bool conusFor0x4Sky) {

    PipelineTemplate pipelineTemplate;
    pipelineTemplate.depthWrite = false;
    pipelineTemplate.depthCulling = true;
    pipelineTemplate.backFaceCulling = false;
    pipelineTemplate.blendMode = conusFor0x4Sky ? EGxBlendEnum::GxBlend_Alpha : EGxBlendEnum::GxBlend_Opaque;
    pipelineTemplate.element = DrawElementMode::TRIANGLE_STRIP;

    auto material = sceneRenderer->createSkyMeshMaterial(pipelineTemplate);

    gMeshTemplate meshTemplate(skyBindings);

    meshTemplate.meshType = MeshType::eGeneralMesh;

    if (conusFor0x4Sky) {
        meshTemplate.start = 198 * 2;
        meshTemplate.end = 102;
    } else {
        meshTemplate.start = 0;
        meshTemplate.end = 300;
    }

    //Make mesh
    HGMesh hmesh = sceneRenderer->createMesh(meshTemplate, material);
    return {hmesh, material};
}

// Unit quad for planet billboards (xy = corner, zw = uv); drawn camera-facing in planetShader
static const std::array<mathfu::vec4, 4> planetQuadVBO = {{
    mathfu::vec4(-0.5f, -0.5f, 0.0f, 1.0f),
    mathfu::vec4( 0.5f, -0.5f, 1.0f, 1.0f),
    mathfu::vec4( 0.5f,  0.5f, 1.0f, 0.0f),
    mathfu::vec4(-0.5f,  0.5f, 0.0f, 0.0f),
}};
static const std::array<uint16_t, 6> planetQuadIBO = {{0, 1, 2, 0, 2, 3}};

HGVertexBufferBindings createPlanetBindings(const HMapSceneBufferCreate &sceneRenderer) {
    auto planetIBO = sceneRenderer->createSkyIndexBuffer(planetQuadIBO.size() * sizeof(uint16_t));
    planetIBO->uploadData(
        planetQuadIBO.data(),
        planetQuadIBO.size() * sizeof(uint16_t));

    auto planetVBO = sceneRenderer->createSkyVertexBuffer(planetQuadVBO.size() * sizeof(mathfu::vec4_packed));
    planetVBO->uploadData(
        planetQuadVBO.data(),
        planetQuadVBO.size() * sizeof(mathfu::vec4_packed)
    );

    return sceneRenderer->createSkyVAO(planetVBO, planetIBO);
}

std::tuple<HGMesh, std::shared_ptr<IPlanetMaterial>> createPlanetMesh(const HMapSceneBufferCreate &sceneRenderer,
                                                                      const HGVertexBufferBindings &planetBindings,
                                                                      const HGSamplableTexture &texture) {

    PipelineTemplate pipelineTemplate;
    pipelineTemplate.depthWrite = false;
    pipelineTemplate.depthCulling = true;
    pipelineTemplate.backFaceCulling = false;
    pipelineTemplate.blendMode = EGxBlendEnum::GxBlend_Alpha;
    pipelineTemplate.element = DrawElementMode::TRIANGLES;

    auto material = sceneRenderer->createPlanetMaterial(pipelineTemplate, texture);

    gMeshTemplate meshTemplate(planetBindings);
    meshTemplate.meshType = MeshType::eGeneralMesh;
    meshTemplate.start = 0;
    meshTemplate.end = 6;

    HGMesh hmesh = sceneRenderer->createMesh(meshTemplate, material);
    return {hmesh, material};
}

void Map::setBakeView(bool on, float x, float y, float half) {
    m_bakeX = x;
    m_bakeY = y;
    m_bakeHalf = half;
    m_bakeOn = on;
}

bool Map::wdlGround(float x, float y, float &height) {
    createFarTerrain();
    return m_farTerrain != nullptr && m_farTerrain->groundHeight(x, y, height);
}

/*
robots@Impl: What a height bake waits on.

The square around the bake camera reaches the tile under it and its neighbours, so the count is taken over those nine: a tile that exists
and has not loaded is pending, and a loaded one adds what AdtObject::countBakeResidency finds in its placements. A neighbour's WMO that
reaches into the square is counted by its own tile. A tile with no object yet is a tile the map has none of, or one the frustum has not
asked for, which the next frame's count catches. Taken on the culling thread, the one that makes the tiles, so the table is read without
a lock.
*/
MapRenderPlan::BakeResidency Map::bakeResidency(float x, float y, float half) {
    MapRenderPlan::BakeResidency result;
    result.valid = true;
    result.x = x;
    result.y = y;

    const int row = worldCoordinateToAdtIndex(x);
    const int col = worldCoordinateToAdtIndex(y);
    for (int r = row - 1; r <= row + 1; r++) {
        for (int c = col - 1; c <= col + 1; c++) {
            if (r < 0 || r > 63 || c < 0 || c > 63) continue;

            const auto &tile = mapTiles[c][r];
            if (tile == nullptr) {
                if (r == row && c == col) {
                    result.total++;
                    result.pending++;
                }
                continue;
            }
            result.total++;
            if (!tile->isLoaded()) {
                result.pending++;
                continue;
            }
            tile->countBakeResidency(x - half, x + half, y - half, y + half, result.total, result.pending, result.pendingBy);
        }
    }
    return result;
}

void Map::makeFramePlan(const FrameInputParams<MapSceneParams> &frameInputParams, const HMapRenderPlan &mapRenderPlan) {
    ZoneScoped ;

    Config* config = this->m_api->getConfig();

    mathfu::vec4 cameraPos = frameInputParams.frameParameters->matricesForCulling->cameraPos;
    mathfu::vec3 cameraVec3 = cameraPos.xyz();
    mathfu::mat4 &frustumMat = frameInputParams.frameParameters->matricesForCulling->perspectiveMat;
    mathfu::mat4 &lookAtMat4 = frameInputParams.frameParameters->matricesForCulling->lookAtMat;

    mapRenderPlan->renderingMatrices = frameInputParams.frameParameters->renderTargets[0].cameraMatricesForRendering;
    mapRenderPlan->deltaTime = frameInputParams.delta;

    size_t adtRenderedThisFramePrev = mapRenderPlan->adtArray.size();
    mapRenderPlan->adtArray = {};
    mapRenderPlan->adtPreload = {};
    mapRenderPlan->adtArray.reserve(adtRenderedThisFramePrev);

    mathfu::mat4 viewPerspectiveMat = frustumMat*lookAtMat4;
    mathfu::vec4 &camera4 = cameraPos;

//    auto oldPlanes = MathHelper::getFrustumClipsFromMatrix(viewPerspectiveMat);
//    auto newPlanes = MathHelper::getFrustumClipsFromMatrix(frustumMat);
//    for (int i = 0; i < newPlanes.size(); i++) {
//        newPlanes[i] = (lookAtMat4.Transpose().Inverse()) * newPlanes[i];
//    }

    MathHelper::PlanesUndPoints planesUndPoints;
    planesUndPoints.planes = MathHelper::getFrustumClipsFromMatrix(viewPerspectiveMat);
    planesUndPoints.points = MathHelper::calculateFrustumPointsFromMat(viewPerspectiveMat);
    auto testPoints = MathHelper::getIntersectionPointsFromPlanes(planesUndPoints.planes);
    planesUndPoints.hullLines = MathHelper::getHullLines(planesUndPoints.points);

    MathHelper::FrustumCullingData frustumData;
    frustumData.frustums = {planesUndPoints};
    frustumData.perspectiveMat = frustumMat;
    frustumData.viewMat = lookAtMat4;
    frustumData.cameraPos = cameraVec3;
    frustumData.farPlane = planesUndPoints.planes[planesUndPoints.planes.size() - 2]; //farPlane is always one before last

    mapRenderPlan->m_currentInteriorGroups = {};
    mapRenderPlan->m_currentWMO = emptyWMO;

    int bspNodeId = -1;
    int interiorGroupNum = -1;
    mapRenderPlan->m_currentWmoGroup = -1;

    //Get potential WMO
    WMOListContainer potentialWmo;
    M2ObjectListContainer potentialM2;

    {
        ZoneScopedN("cullGetCurrentWMOCounter");
        //Hack that is needed to get the current WMO the camera is in. Basically it does frustum culling over current ADT
        getPotentialEntities(frustumData, cameraPos, mapRenderPlan, potentialM2, potentialWmo);

        float bottomBorder = -99999;

        //Get bottom border from ADT
        {
            float adtHeight = -99999;
            getPossibleHeight(cameraPos, adtHeight);
            mapRenderPlan->hasGroundHeight = adtHeight > -99999;
            mapRenderPlan->groundHeight = adtHeight;

            if (adtHeight < cameraPos.z)
                bottomBorder = adtHeight;
        }

        WmoGroupResult resGroupResult;
        WMOObjId resWmoId = emptyWMO;
        bool found = false;
        for (auto &wmoId: potentialWmo.getCandidates()) {
            auto checkingWmoObj = wmoFactory->getObjectById<0>(wmoId);
            if (checkingWmoObj == nullptr) continue;

            WmoGroupResult groupResult;
            bool result = checkingWmoObj->getGroupWmoThatCameraIsInside(camera4, groupResult, bottomBorder);

            if (!result) continue;

            found = true;
            resGroupResult = groupResult;
            resWmoId = wmoId; //
        }

        if (found) {
            auto checkingWmoObj = wmoFactory->getObjectById<0>(resWmoId);
            if (checkingWmoObj) {
                mapRenderPlan->m_currentWMO = resWmoId;
                mapRenderPlan->m_currentWmoGroup = resGroupResult.groupIndex;
                if (checkingWmoObj->isGroupWmoInterior(resGroupResult.groupIndex)) {
                    mapRenderPlan->m_currentInteriorGroups.push_back(resGroupResult);
                    interiorGroupNum = resGroupResult.groupIndex;
                    mapRenderPlan->currentWmoGroupIsExtLit = checkingWmoObj->isGroupWmoExteriorLit(
                        resGroupResult.groupIndex);
                    mapRenderPlan->currentWmoGroupShowExtSkybox = checkingWmoObj->isGroupWmoExtSkybox(
                        resGroupResult.groupIndex);
                } else {
                }
                bspNodeId = resGroupResult.nodeId;
            }
        }
    }


    //7. Get AreaId and Area Name
    AreaRecord wmoAreaRecord;
    bool wmoAreaFound = false;
    if (mapRenderPlan->m_currentWMO != emptyWMO) {
        auto l_currentWmoObject = wmoFactory->getObjectById<0>(mapRenderPlan->m_currentWMO);
        if (l_currentWmoObject != nullptr) {
            auto nameId = l_currentWmoObject->getNameSet();
            auto wmoId = l_currentWmoObject->getWmoId();
            auto groupId = l_currentWmoObject->getWmoGroupId(mapRenderPlan->m_currentWmoGroup);

            if (m_api->databaseHandler != nullptr) {
                wmoAreaFound = m_api->databaseHandler->getWmoArea(wmoId, nameId, groupId, wmoAreaRecord);
            }
        }
    }

    AreaRecord areaRecord;
    if ((m_api->databaseHandler != nullptr)) {
        if (wmoAreaRecord.areaId == 0) {
            if (mapRenderPlan->adtAreadId > 0) {
                areaRecord = m_api->databaseHandler->getArea(mapRenderPlan->adtAreadId);
            }
        } else {
            areaRecord = m_api->databaseHandler->getArea(wmoAreaRecord.areaId);
        }
    }


    mapRenderPlan->wmoAreaName = wmoAreaRecord.areaName;
    mapRenderPlan->areaName = areaRecord.areaName;

    auto &stateForConditions = mapRenderPlan->frameDependentData->stateForConditions;

    stateForConditions.currentAreaId = areaRecord.areaId;
    stateForConditions.currentParentAreaId = areaRecord.parentAreaId;

    mapRenderPlan->areaId = areaRecord.areaId;
    mapRenderPlan->parentAreaId = areaRecord.parentAreaId;


    //Get lights from DB
    updateLightAndSkyboxData(mapRenderPlan, frustumData, stateForConditions, areaRecord);

    ///-----------------------------------


    auto lcurrentWMO = wmoFactory->getObjectById<0>(mapRenderPlan->m_currentWMO);
    auto currentWmoGroup = mapRenderPlan->m_currentWmoGroup;

    //The camera's group lights and fogs the frame and picks its sky, and draws nothing: every WMO group is culled by
    //checkExterior, whichever group the camera is in
    if ((lcurrentWMO != nullptr) && (!mapRenderPlan->m_currentInteriorGroups.empty()) && (lcurrentWMO->isLoaded())) {
        if (auto wmoSkyBox = lcurrentWMO->getSkyBoxForGroup(mapRenderPlan->m_currentInteriorGroups[0].groupIndex)) {
            mapRenderPlan->viewsHolder.getSkybox()->m2List.addToDraw(wmoSkyBox);
        }
    }

    // The far terrain's share of the view and whether it is alone come from Config, read together once here so the tile requests, the
    // plan and the draw agree. Alone, checkExterior and the preload ask for no tile, model or spawn.
    m_farTerrainOnly = false;
    float farShare = 1.0f;
    if (config->farTerrain) {
        createFarTerrain();
        if (m_farTerrain != nullptr) {
            farShare = std::clamp(config->farTerrainShare, 0.0f, 1.0f);
            m_farTerrainOnly = config->farTerrainOnly;
        }
    }

    {
        auto exteriorView = mapRenderPlan->viewsHolder.getOrCreateExterior(frustumData);
        checkExterior(cameraPos, exteriorView->frustumData, mapRenderPlan);
    }

    //After every visible tile has been requested, so the visible ones are first in line
    if (frameInputParams.frameParameters->preloadPoint && !m_farTerrainOnly) {
        preloadTilesAhead(*frameInputParams.frameParameters->preloadPoint, mapRenderPlan);
    }

    mapRenderPlan->tilesPending = false;
    for (const auto &adt : mapRenderPlan->adtArray) {
        if (!adt.adtObject->isLoaded()) {
            mapRenderPlan->tilesPending = true;
            break;
        }
    }

    mapRenderPlan->bake = {};
    if (m_bakeOn) mapRenderPlan->bake = bakeResidency(cameraVec3.x, cameraVec3.y, m_bakeHalf);

    if (config->farTerrain) {
        if (m_farTerrain != nullptr) {
            float wdlGround = 0.0f;
            mapRenderPlan->hasWdlGround = m_farTerrain->groundHeight(cameraVec3.x, cameraVec3.y, wdlGround);
            mapRenderPlan->wdlGround = wdlGround;

            FarTerrain::View farView;
            farView.cameraPos = cameraVec3;
            farView.lookAt = lookAtMat4;
            farView.perspective = frustumMat;
            farView.drawDistance = config->farPlaneForCulling;
            farView.share = farShare;
            farView.only = m_farTerrainOnly;
            m_farTerrain->makeFrame(*config, farView, mapRenderPlan->adtArray, mapRenderPlan->farTerrain);
            m_farTerrain->checkAdtCorners(mapRenderPlan->adtArray);
        } else {
            mapRenderPlan->farTerrain.on = true;
        }
    }

    if (!m_suppressDrawingSky && (mapRenderPlan->viewsHolder.getExterior() != nullptr || mapRenderPlan->currentWmoGroupShowExtSkybox || mapRenderPlan->currentWmoGroupIsExtLit)) {
        ZoneScopedN("Skybox");
        auto exteriorView = mapRenderPlan->viewsHolder.getOrCreateExterior(frustumData);

        if (m_wdlObject != nullptr && config->renderSkyScene) {
            m_wdlObject->checkSkyScenes(
                stateForConditions,
                exteriorView->m2List,
                cameraPos,
                frustumData);
        }

        mapRenderPlan->renderSky = m_api->getConfig()->renderSkyDom;

        if (config->renderSkyDom) {
            auto skyBoxView = mapRenderPlan->viewsHolder.getSkybox();
            const auto &starsData = mapRenderPlan->frameDependentData->stars;
            if (starsData.enabled) {
                if (m_starsModel == nullptr) {
                    m_starsModel = m2Factory->createObject(m_api, true);
                    m_starsModel->setLoadParams(0, {}, {});
                    m_starsModel->setModelFileId(130629);
                    m_starsModel->createPlacementMatrix(mathfu::vec3(0, 0, 0), 0, mathfu::vec3(1, 1, 1), nullptr);
                    m_starsModel->calcWorldPosition();
                }
                m_starsModel->setAlpha(starsData.alpha);

                skyBoxView->stars.addToDraw(m_starsModel);
            }

            auto &exteriorSkyBoxes = m_dayNightLightHolder.getExteriorSkyBoxes();
            if (!exteriorSkyBoxes.empty() && config->renderSkyDom) {

                for (auto &model: exteriorSkyBoxes) {
                    if (model != nullptr) {
                        skyBoxView->m2List.addToDraw(model);
                    }
                }
            }

            skyBoxView->skyMesh = skyMesh;

            if (mapRenderPlan->renderSky) {
                const auto &fdd = mapRenderPlan->frameDependentData;
                for (int i = 0; i < 3; i++) {
                    skyBoxView->m_planetMeshes[i] = fdd->planets[i].visible ? m_planetMeshes[i] : nullptr;
                }
            } else {
                for (int i = 0; i < 3; i++)
                    skyBoxView->m_planetMeshes[i] = nullptr;
            }

            if (mapRenderPlan->frameDependentData->overrideValuesWithFinalFog) {
                skyBoxView->skyMesh0x4 = skyMesh0x4Sky;
            }
        }
    }

    std::vector<std::shared_ptr<CEngineLight>> newWmoLights = {};
    {

        auto exteriorView = mapRenderPlan->viewsHolder.getExterior();
        if (exteriorView != nullptr) {
            {
                ZoneScopedN("collect m2 from groups");
                exteriorView->addM2FromGroups(frustumData, cameraPos, m_api->getConfig());
            }
            {
                ZoneScopedN("m2AndWMOArr merge");
                mapRenderPlan->m2Array.addDrawnAndToLoad(exteriorView->m2List);
                mapRenderPlan->wmoGroupArray.addToLoadAndDraw(exteriorView->wmoGroupArray);
            }

            exteriorView->collectLights(mapRenderPlan->pointLights, mapRenderPlan->spotLights, newWmoLights);
        }
    }

    {
        ZoneScopedN("process new lights");
        //Delete duplicates for new wmo lights
        std::sort(newWmoLights.begin(), newWmoLights.end());
        newWmoLights.erase(std::unique(newWmoLights.begin(), newWmoLights.end()), newWmoLights.end());

        //Collect spotLights and point lights from new WMO lights
        auto sceneTime = getCurrentSceneTime();
        for (auto &newWmoLight: newWmoLights) {
            newWmoLight->collectLight(cameraVec3, sceneTime, mapRenderPlan->pointLights, mapRenderPlan->spotLights, mapRenderPlan->insideSpotLights);
        }
    }



//    //Limit M2 count based on distance/m2 height
//    for (auto it = this->m2RenderedThisFrameArr.begin();
//         it != this->m2RenderedThisFrameArr.end();) {
//        if ((*it)->getCurrentDistance() > ((*it)->getHeight() * 5)) {
//            it = this->m2RenderedThisFrameArr.erase(it);
//        } else {
//            ++it;
//        }
//    }
}

void Map::updateLightAndSkyboxData(const HMapRenderPlan &mapRenderPlan, MathHelper::FrustumCullingData &frustumData,
                                   StateForConditions &stateForConditions, const AreaRecord &areaRecord) {

    m_dayNightLightHolder.updateLightAndSkyboxData(mapRenderPlan, frustumData, stateForConditions, areaRecord);
}

void Map::getPotentialEntities(const MathHelper::FrustumCullingData &frustumData,
                               const mathfu::vec4 &cameraPos,
                               const HMapRenderPlan &mapRenderPlan,
                               M2ObjectListContainer &potentialM2,
                               WMOListContainer &potentialWmo) {

    if (m_wdtfile->getStatus() == FileStatus::FSLoaded) {
        if (!m_wdtfile->mphd->flags.wdt_uses_global_map_obj) {
            int adt_x = worldCoordinateToAdtIndex(cameraPos.y);
            int adt_y = worldCoordinateToAdtIndex(cameraPos.x);
            if ((adt_x >= 64) || (adt_x < 0)) return;
            if ((adt_y >= 64) || (adt_y < 0)) return;

            mapRenderPlan->adtAreadId = -1;
            auto &adtObjectCameraAt = mapTiles[adt_x][adt_y];

            if (adtObjectCameraAt != nullptr) {
                ADTObjRenderRes tempRes;
                tempRes.adtObject = adtObjectCameraAt;
                adtObjectCameraAt->checkReferences(
                    tempRes,
                    cameraPos,
                    frustumData,
                    5,
                    potentialM2,
                    potentialWmo,
                    0,
                    0,
                    16,
                    16
                );

                int adt_global_x = worldCoordinateToGlobalAdtChunk(cameraPos.y) % 16;
                int adt_global_y = worldCoordinateToGlobalAdtChunk(cameraPos.x) % 16;

                mapRenderPlan->adtAreadId = adtObjectCameraAt->getAreaId(adt_global_x, adt_global_y);
            }
        } else {
            if (wmoMap == nullptr) {
                wmoMap = wmoFactory->createObject(m_api);

                //The wmo maps should be loaded with coordinates as is.
                //So we need to apply anti ADT fix, which is applied down the line
                SMMapObjDef fixedWmoDef = *m_wdtfile->wmoDef;
                fixedWmoDef.position =
                    (MathHelper::getAdtToWorldMat4().Inverse() * mathfu::vec4(mathfu::vec3(fixedWmoDef.position), 1.0f)).xyz();

                int zero;
                wmoMap->setLoadingParam(fixedWmoDef, {zero}, {zero});
                wmoMap->setModelFileId(m_wdtfile->wmoDef->nameId);
            }

            potentialWmo.addCand(wmoMap);
        }
    }
}


void Map::getAdtAreaId(const mathfu::vec4 &cameraPos, int &areaId, int &parentAreaId) {
    areaId = 0;
    parentAreaId = 0;

    if (m_wdtfile->getStatus() == FileStatus::FSLoaded) {
        if (!m_wdtfile->mphd->flags.wdt_uses_global_map_obj) {
            int adt_x = worldCoordinateToAdtIndex(cameraPos.y);
            int adt_y = worldCoordinateToAdtIndex(cameraPos.x);
            if ((adt_x >= 64) || (adt_x < 0)) return;
            if ((adt_y >= 64) || (adt_y < 0)) return;

            auto &adtObjectCameraAt = mapTiles[adt_x][adt_y];

            int adt_global_x = worldCoordinateToGlobalAdtChunk(cameraPos.y) % 16;
            int adt_global_y = worldCoordinateToGlobalAdtChunk(cameraPos.x) % 16;

            areaId = adtObjectCameraAt->getAreaId(adt_global_x, adt_global_y);

            if (areaId > 0 && (m_api->databaseHandler != nullptr)) {
                auto areaRecord = m_api->databaseHandler->getArea(areaId);
                parentAreaId = areaRecord.parentAreaId;
            }
        }
    }
}

void Map::checkExterior(mathfu::vec4 &cameraPos,
                        const MathHelper::FrustumCullingData &frustumData,
                        const HMapRenderPlan &mapRenderPlan
) {
    ZoneScoped ;
//    std::cout << "Map::checkExterior finished called" << std::endl;
    if (m_wdlObject == nullptr && m_wdtfile != nullptr && m_wdtfile->getStatus() == FileStatus::FSLoaded) {
        if (m_wdtfile->mphd->flags.wdt_has_maid) {
            m_wdlObject = std::make_shared<WdlObject>(m_api, m_wdtfile->mphd->wdlFileDataID);
            m_wdlObject->setMapApi(this);
        }
    }

    if (m_wdtLightObject == nullptr && m_wdtfile != nullptr && m_wdtfile->getStatus() == FileStatus::FSLoaded) {
        if (m_wdtfile->mphd->flags.wdt_has_maid) {
            m_wdtLightObject = std::make_shared<WdtLightsObject>(m_api, m_wdtfile->mphd->lgtFileDataID);
        }
    }

    auto exteriorView = mapRenderPlan->viewsHolder.getExterior(); //Should not be null, since we called checkExterior

    // The far-terrain-alone view has no tile, model or spawn: the WDL's low-detail models, the tiles and the spawns are all left out. The WDL
    // still gets its call, which builds its sky scenes on the frame its file has loaded, and its candidates go nowhere.
    if (m_wdlObject != nullptr) {
        if (m_farTerrainOnly) {
            M2ObjectListContainer noM2;
            WMOListContainer noWmo;
            m_wdlObject->checkFrustumCulling(frustumData, cameraPos, noM2, noWmo);
        } else {
            m_wdlObject->checkFrustumCulling(frustumData, cameraPos, exteriorView->m2List, mapRenderPlan->wmoArray);
        }
    }

    if (!m_farTerrainOnly) {
        getCandidatesEntities(frustumData, cameraPos, mapRenderPlan, exteriorView->m2List, mapRenderPlan->wmoArray);
    }

    if (m_worldObjectManager && !m_farTerrainOnly) {
        ZoneScopedN("world objects candidates");
        m_worldObjectManager->cullAndCollect(exteriorView->m2List);
    }

    //Frustum cull
    auto &wmoCandidates = mapRenderPlan->wmoArray.getCandidates();
    std::vector<uint32_t> wmoCullResults;

    oneapi::tbb::task_arena cullingArena(m_api->getConfig()->hardwareThreadCount(), 1);

    //Batch frustum-cull WMO candidates by their world bounding boxes, so that off-frustum WMOs
    //don't pay for the per-group culling. The WMO bbox contains all of its groups (loaded
    //or not), so this only rejects WMOs whose groups would all be culled downstream anyway.
    if (!wmoCandidates.empty()) {
        ZoneScopedN("Cull WMO");
        wmoCullResults = std::vector<uint32_t>(wmoCandidates.size(), 0xFFFFFFFF);

#if (__AVX__ && __SSE2__)
        ObjectCullingSEE<WMOObjId>::cull(frustumData, 0, wmoCandidates.size(), wmoCandidates, wmoCullResults);
#else
        ObjectCulling<WMOObjId>::cull(frustumData, 0, wmoCandidates.size(), wmoCandidates, wmoCullResults);
#endif
    }

    const bool renderAntiPortals = m_api->getConfig()->renderAntiPortals;
    for (int wmoCandInd = 0; wmoCandInd < wmoCandidates.size(); wmoCandInd++) {
        auto &wmoId = wmoCandidates[wmoCandInd];
        auto wmoCandidate = wmoFactory->getObjectById<0>(wmoId);
        if (wmoCandidate!= nullptr && !wmoCandidate->isLoaded()) continue;

        wmoCandidate->queueCreatureGroupLoads(mapRenderPlan->wmoGroupArray);

        if (wmoCullResults[wmoCandInd] != 0) {
            //WMO bbox is outside the frustum. WMOs with unconditional per-group handling still
            //need the full group pass; for the rest only the load triggers of not-yet-loaded
            //preloaded groups have to be preserved.
            if (!wmoCandidate->hasAlwaysDrawGroups() &&
                !(renderAntiPortals && wmoCandidate->hasAntiportalGroups())) {
                wmoCandidate->triggerExteriorGroupLoads(exteriorView->wmoGroupArray);
                continue;
            }
        }

        if (wmoCandidate->collectGroups(cameraPos, frustumData, *exteriorView)) {
            mapRenderPlan->wmoArray.addToDrawn(wmoCandidate);
        }
    }

    //3.2 Iterate over all global WMOs and M2s (they have uniqueIds)
    {
        ZoneScopedN("Cull M2");
        auto results = std::vector<uint32_t>();

        auto &candidates = exteriorView->m2List.getCandidates();
        // Models too small on screen, or too small in the world, are skipped like ones outside the frustum.
        const float minScreenPercent = m_api->getConfig()->minM2ScreenPercent;
        const float fadeStartPercent = m_api->getConfig()->m2FadeStartPercent;
        const float minWorldSize = m_api->getConfig()->minM2WorldSize;
        const float tanHalfFov = std::tan(toRadian(m_api->getConfig()->fov) * 0.5f);
        const mathfu::vec3 cullCameraPos = cameraPos.xyz();
        if (candidates.size() > 0) {
            results = std::vector<uint32_t>(candidates.size(), 0xFFFFFFFF);

            cullingArena.execute([&] {
                oneapi::tbb::parallel_for(tbb::blocked_range<size_t>(0, candidates.size(), 2000),
                                          [&](tbb::blocked_range<size_t> &r) {
    //            for (size_t i = r.begin(); i != r.end(); ++i) {
    //                auto &m2ObjectCandidate = tmpVector[i];
    //                if(m2ObjectCandidate->checkFrustumCulling(cameraPos, frustumData)) {
    //                    exteriorView->drawnM2s.insert(m2ObjectCandidate);
    //                    cullStage->m2Array.insert(m2ObjectCandidate);
    //                }
#if (__AVX__ && __SSE2__)
                      ObjectCullingSEE<M2ObjId>::cull(frustumData,
                                                                        r.begin(), r.end(), candidates,
                                                                        results);
#else
                      ObjectCulling<M2ObjId>::cull(frustumData,
                                                                     r.begin(), r.end(), candidates,
                                                                     results);
#endif
                      ScreenSizeCulling<M2ObjId>::cull(cullCameraPos, tanHalfFov, minScreenPercent,
                                                       r.begin(), r.end(), candidates, results, fadeStartPercent, minWorldSize);
    //            }
                }, tbb::auto_partitioner());
            });
        }

        for (int i = 0; i < candidates.size(); i++) {
            if (!results[i]) {
//                auto m2ObjectCandidate = m2Factory->getObjectById<0>(candidates[i]);
                exteriorView->m2List.addToDraw(candidates[i]);
            }
        }
    }
}

void Map::getCandidatesEntities(const MathHelper::FrustumCullingData &frustumData,
                                const mathfu::vec4 &cameraPos,
                                const HMapRenderPlan &mapRenderPlan,
                                M2ObjectListContainer &m2ObjectsCandidates,
                                WMOListContainer &wmoCandidates) {
    ZoneScoped;
    if (m_wdtfile != nullptr && m_wdtfile->getStatus() == FileStatus::FSLoaded) {
        //Get visible area that should be checked
        float minx = 99999, maxx = -99999;
        float miny = 99999, maxy = -99999;

        //    float adt_x = floor((32 - (cameraPos[1] / 533.33333)));
//    float adt_y = floor((32 - (cameraPos[0] / 533.33333)));

//    //FOR DEBUG
//    adt_x_min = worldCoordinateToAdtIndex(cameraPos.y) - 2;
//    adt_x_max = adt_x_min + 4;
//    adt_y_min = worldCoordinateToAdtIndex(cameraPos.x) - 2;
//    adt_y_max = adt_y_min + 4;

//    for (int i = 0; i < 64; i++)
//        for (int j = 0; j < 64; j++) {
//            if (this->m_wdtfile->mapTileTable->mainInfo[i][j].Flag_AllWater) {
//                std::cout << AdtIndexToWorldCoordinate(j) <<" "<<  AdtIndexToWorldCoordinate(i) << std::endl;
//            }
//        }

//    std::cout << AdtIndexToWorldCoordinate(adt_y_min) <<" "<<  AdtIndexToWorldCoordinate(adt_x_min) << std::endl;

        for (auto &frustums : frustumData.frustums) {
            for (int i = 0; i < frustums.points.size(); i++) {
                auto &frustumPoint = frustums.points[i];

                minx = std::min<float>(frustumPoint.x, minx);
                maxx = std::max<float>(frustumPoint.x, maxx);
                miny = std::min<float>(frustumPoint.y, miny);
                maxy = std::max<float>(frustumPoint.y, maxy);
            }
        }

        int adt_x_min = std::max<int>(std::floor(worldCoordinateToAdtIndexF(maxy)), 0);
        int adt_x_max = std::min<int>(std::ceil(worldCoordinateToAdtIndexF(miny)), 63);

        int adt_y_min = std::max<int>(std::floor(worldCoordinateToAdtIndexF(maxx)), 0);
        int adt_y_max = std::min<int>(std::ceil(worldCoordinateToAdtIndexF(minx)), 63);

        if (!m_wdtfile->mphd->flags.wdt_uses_global_map_obj) {
            for (int i = adt_x_min; i <= adt_x_max; i++) {
                for (int j = adt_y_min; j <= adt_y_max; j++) {
                    checkADTCulling(i, j, frustumData, cameraPos, mapRenderPlan, m2ObjectsCandidates, wmoCandidates);
                }
            }
            for (auto &mandatoryAdt : m_mandatoryADT) {
                checkADTCulling(mandatoryAdt[0], mandatoryAdt[1], frustumData, cameraPos, mapRenderPlan, m2ObjectsCandidates, wmoCandidates);
            }

        } else if (wmoMap) {
            wmoCandidates.addCand(wmoMap);
            collectMapCreatures(m2ObjectsCandidates, wmoCandidates);
        }
    }
}

/*
robots@Impl: Creatures and game objects of a map with no tiles.

A map whose WDT is one global WMO (a dungeon or raid such as Ragefire Chasm) has no ADT to carry its creatures, so the map
reads every CreatureSpawn row of its id the first time its exterior is collected, and builds each as AdtObject does for a tile
(WorldObjectManager::createCreature), offering all to the global WMO. The GameObjectSpawn rows of the id go the same way
(WorldObjectManager::createGameObjectSpawn). The ones it leaves outside join the exterior candidates of each frame, which the map's
per-object culling treats as any M2, and go when the map does. A game object whose model is a WMO (IsWmo) is a WMO candidate of each
frame instead (WorldObjectManager::createGameObjectWmo).
*/
void Map::collectMapCreatures(M2ObjectListContainer &m2ObjectsCandidates, WMOListContainer &wmoCandidates) {
    if (!m_mapCreaturesLoaded) {
        m_mapCreaturesLoaded = true;
        if (m_api->databaseHandler == nullptr) return;

        std::vector<CreatureSpawnRecord> spawns;
        m_api->databaseHandler->getCreatureSpawnsForMap(m_mapId, spawns);
        for (auto &spawn : spawns) {
            auto creature = WorldObjectManager::createCreature(m_api, spawn);
            if (creature == nullptr) continue;

            WorldObjectManager::homeCreature(creature, spawn.Pos, {wmoMap}, false);
            m_mapCreatures.push_back(creature);
        }
        const size_t creatures = m_mapCreatures.size();

        std::vector<GameObjectSpawnRecord> gameObjects;
        m_api->databaseHandler->getGameObjectSpawnsForMap(m_mapId, gameObjects);
        for (auto &spawn : gameObjects) {
            if (spawn.IsWmo) {
                auto wmo = WorldObjectManager::createGameObjectWmo(m_api, spawn, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f});
                if (wmo != nullptr) m_mapGameObjectWmos.push_back({wmo, spawn.Events});
                continue;
            }

            auto gameObject = WorldObjectManager::createGameObjectSpawn(m_api, spawn);
            if (gameObject == nullptr) continue;

            WorldObjectManager::homeCreature(gameObject, spawn.Pos, {wmoMap}, false);
            m_mapCreatures.push_back(gameObject);
        }
        std::cerr << "flyover creatures map " << m_mapId << ": " << creatures << " placed, game objects "
                  << m_mapCreatures.size() - creatures << " placed, " << m_mapGameObjectWmos.size() << " WMO game objects" << std::endl;
    }
    for (auto const &placed : m_mapGameObjectWmos) {
        if (placed.shown(*m_api->getConfig())) wmoCandidates.addCand(placed.wmo);
    }

    for (auto const &creature : m_mapCreatures) {
        if (creature->home == CreatureHome::Exterior && creature->shown(*m_api->getConfig()))
            m2ObjectsCandidates.addCandidate(creature->id);
    }
}

void Map::checkADTCulling(int i, int j,
                          const MathHelper::FrustumCullingData &frustumData,
                          const mathfu::vec4 &cameraPos,
                          const HMapRenderPlan &mapRenderPlan,
                          M2ObjectListContainer &m2ObjectsCandidates,
                          WMOListContainer &wmoCandidates) {
    if ((i < 0) || (i >= 64)) return;
    if ((j < 0) || (j >= 64)) return;

    auto &adtObject = mapTiles[i][j];
    auto &adtArray = mapRenderPlan->adtArray;
    if (adtObject != nullptr) {
        //A tile whose box is out of view collects nothing, but stays alive as any tile in range does: the tile frees by
        //being left untouched, and a tile freed here would load again the moment the camera turned. A tile still loading
        //has no box and is let through.
        CAaBox cullBox;
        if (adtObject->getCullBox(cullBox) && !MathHelper::checkFrustum(frustumData, cullBox)) {
            adtObject->getFreeStrategy()(false, true, getCurrentSceneTime());
            return;
        }

        auto &adtFrustRes = adtArray.emplace_back();
        adtFrustRes.adtObject = adtObject;

        bool result = adtObject->checkFrustumCulling(
            adtFrustRes,
            cameraPos,
            frustumData, m2ObjectsCandidates, wmoCandidates);

//        if (this->m_adtBBHolder != nullptr) {
//            //When adt passes BBHolder test, consider it influences the picture even if checkFrustumCulling
//            //is false. So do forceful update for freeStrategy
//            adtObject->getFreeStrategy()(false, true, this->getCurrentSceneTime());
//        }

        if (!result) {
            adtArray.pop_back();
        } else {
            //Add lights from WDTLightObject
            if (m_wdtLightObject) {
                //Get lights
                auto sceneTime = getCurrentSceneTime();
                m_wdtLightObject->collectLights(
                    cameraPos.xyz(),
                    sceneTime,
                    i, j,
                    mapRenderPlan->pointLights,
                    mapRenderPlan->spotLights, mapRenderPlan->insideSpotLights);
            }
        }
    } else if (!m_lockedMap && true) { //(m_wdtfile->mapTileTable->mainInfo[j][i].Flag_HasADT > 0) {
        createAdtTile(i, j);
    }
}

void Map::createAdtTile(int i, int j) {
    auto &adtObject = mapTiles[i][j];

    useWeightedBlend = m_wdtfile->mphd->flags.adt_has_height_texturing || m_wdtfile->mphd->flags.adt_has_big_alpha;
    if (m_wdtfile->mphd->flags.wdt_has_maid) {
        auto &mapFileIds = m_wdtfile->mapFileDataIDs->array[j * 64 + i];
        if (mapFileIds.rootADT > 0) {
            adtObject = adtObjectFactory->createObject(m_api, i, j,
                                                    m_wdtfile->mapFileDataIDs->array[j * 64 + i],
                                                    useWeightedBlend,
                                                    m_wdtfile);
        } else {
            return;
        }
    } else {
        std::string adtFileTemplate =
            "world/maps/" + mapName + "/" + mapName + "_" + std::to_string(i) + "_" +
            std::to_string(j);
        adtObject = adtObjectFactory->createObject(m_api, adtFileTemplate, mapName, i, j,
                                                  useWeightedBlend,
                                                  m_wdtfile);
    }

    adtObject->setMapApi(this);
    adtObject->setFreeStrategy(zeroStateLambda);
}

/*
robots@Impl: Tile preload ahead of the camera.

The tile under the point and its eight neighbours are created, which starts their file requests, and each frame
the free strategy is refreshed for them, so a tile outlives the adtTTLWithoutUpdate that would otherwise free it
before the camera arrives. They go into the plan's adtPreload, not adtArray: doPostLoad uploads them after the
visible tiles within the same per-frame cap, and nothing collects or updates them. A tile already in adtArray is
the visible camera's and is left to it. Tiles are refreshed only while the point is within one tile of them,
and from there the real frustum takes over.
*/
void Map::preloadTilesAhead(const mathfu::vec3 &point, const HMapRenderPlan &mapRenderPlan) {
    if (m_lockedMap || m_wdtfile == nullptr || m_wdtfile->getStatus() != FileStatus::FSLoaded) return;
    if (m_wdtfile->mphd->flags.wdt_uses_global_map_obj) return;

    int centerX = worldCoordinateToAdtIndex(point.y);
    int centerY = worldCoordinateToAdtIndex(point.x);
    auto sceneTime = getCurrentSceneTime();

    for (int i = centerX - 1; i <= centerX + 1; i++) {
        for (int j = centerY - 1; j <= centerY + 1; j++) {
            if ((i < 0) || (i > 63)) continue;
            if ((j < 0) || (j > 63)) continue;

            if (mapTiles[i][j] == nullptr) {
                createAdtTile(i, j);
            }

            auto adtObject = mapTiles[i][j];
            if (adtObject == nullptr) continue;

            bool visible = false;
            for (const auto &adtRes : mapRenderPlan->adtArray) {
                if (adtRes.adtObject == adtObject) {
                    visible = true;
                    break;
                }
            }
            if (visible) continue;

            adtObject->getFreeStrategy()(false, true, sceneTime);
            mapRenderPlan->adtPreload.push_back(adtObject);
        }
    }
}

void Map::createFarTerrain() {
    if (m_farTerrain != nullptr || m_wdlObject == nullptr || m_wdtfile == nullptr) return;
    if (m_wdtfile->getStatus() != FileStatus::FSLoaded || m_wdtfile->mphd->flags.wdt_uses_global_map_obj) return;

    const auto &wdl = m_wdlObject->getWdlFile();
    if (wdl == nullptr) return;

    m_farTerrain = std::make_shared<FarTerrain>(m_api, wdl, m_wdtfile, m_mapId, mapName);
}

void Map::getPossibleHeight(const mathfu::vec4 &cameraPos, float &height) {
    if (m_wdtfile && m_wdtfile->getStatus() == FileStatus::FSLoaded) {
        if (!m_wdtfile->mphd->flags.wdt_uses_global_map_obj) {
            int adt_x = worldCoordinateToAdtIndex(cameraPos.y);
            int adt_y = worldCoordinateToAdtIndex(cameraPos.x);
            if ((adt_x >= 64) || (adt_x < 0)) return;
            if ((adt_y >= 64) || (adt_y < 0)) return;

            auto &adtObjectCameraAt = mapTiles[adt_x][adt_y];

            if (adtObjectCameraAt != nullptr) {
                adtObjectCameraAt->getHeight(cameraPos, height);
            }
        }
    }
}

void Map::createAdtFreeLamdas() {
    FreeStrategy lamda;
    auto *config = m_api->getConfig();
    if (m_api->getConfig()->adtFreeStrategy == EFreeStrategy::eTimeBased) {
        animTime_t l_currentTime = 0;
        lamda = [l_currentTime, config](bool doCheck, bool doUpdate, animTime_t currentTime) mutable -> bool {
            if (doCheck) {
                return (currentTime - l_currentTime) > config->adtTTLWithoutUpdate;
            }
            if (doUpdate) {
                l_currentTime = currentTime;
            }
            return false;
        };
    } else if (m_api->getConfig()->adtFreeStrategy == EFreeStrategy::eFrameBase) {
        int l_currentFrame = 0;
        auto l_hDevice = m_api->hDevice;
        lamda = [l_currentFrame, config, l_hDevice](bool doCheck, bool doUpdate, animTime_t currentTime) mutable -> bool {
            if (doCheck) {
//                return (l_hDevice->getFrameNumber() - l_currentFrame) > config->adtFTLWithoutUpdate;
            }
            if (doUpdate) {
//                l_currentFrame = l_hDevice->getFrameNumber();
            }
            return false;
        };
    }
    adtFreeLambda = lamda;
    zeroStateLambda = lamda;

}

/*
robots@Impl: Finishing loaded geometry against one time budget. M2 geometry, WMO roots and WMO groups share one queue, since
what they cost is the same kind of work, uploads and material creation, and a cap per kind let any one of them eat the frame.

The queue is rebuilt each frame from the plan's toLoad lists, sorted by distance from the camera to the item's box, nearest
first. The WMO the camera stands in, its root and its groups, sorts ahead of everything: it is the view the camera is in, and
a room that is not there is a hole. Ties go root, then group, then model, since a group waits for its root. Items past the
budget are not dropped: they are still not loaded next frame, and the next plan lists them again.

An item that is not ready yet (its file has not arrived) costs a lookup and starts its loading, so it neither spends the
budget nor counts as finished; the clock is read after each item, and the loop leaves only once an item has finished and the
budget is spent, which keeps a frame of unready items from stopping the queue before it reaches one that is ready. Inside the
fast-load window the budget is lifted and the loop runs through the whole queue.
*/
namespace {
struct PostLoadItem {
    enum class Kind : uint8_t { WmoRoot, WmoGroup, M2 };

    bool cameraWmo = false;
    float distanceSq = 0;
    Kind kind = Kind::M2;
    M2ObjId m2Id = M2ObjId(0);
    WMOObjId wmoId = emptyWMO;
    WmoGroupObject *group = nullptr;
};

float distanceSqToBox(const CAaBox &box, const mathfu::vec3 &point) {
    mathfu::vec3 closest = mathfu::vec3::Max(mathfu::vec3(box.min), mathfu::vec3::Min(mathfu::vec3(box.max), point));
    return (closest - point).LengthSquared();
}
}

size_t Map::finishLoadedGeometry(const HMapSceneBufferCreate &sceneRenderer, const HMapRenderPlan &renderPlan, bool fastLoad) {
    ZoneScopedN("Finish loaded geometry");
    const Config *config = m_api->getConfig();
    const mathfu::vec3 cameraPos = renderPlan->renderingMatrices ? renderPlan->renderingMatrices->cameraPos.xyz() : mathfu::vec3(0, 0, 0);
    WmoObject *cameraWmo = wmoFactory->getObjectById<0>(renderPlan->m_currentWMO);

    std::vector<PostLoadItem> queue;

    if (config->renderM2) {
        for (auto &m2ObjectId: renderPlan->m2Array.getToLoadGeom()) {
            PostLoadItem item;
            item.kind = PostLoadItem::Kind::M2;
            item.m2Id = m2ObjectId;
            item.distanceSq = distanceSqToBox(retrieveAABB(m2ObjectId), cameraPos);
            queue.push_back(item);
        }
    }
    if (config->renderWMO) {
        for (const auto wmoId: renderPlan->wmoArray.getToLoad()) {
            PostLoadItem item;
            item.kind = PostLoadItem::Kind::WmoRoot;
            item.wmoId = wmoId;
            item.cameraWmo = cameraWmo != nullptr && wmoId == renderPlan->m_currentWMO;
            item.distanceSq = item.cameraWmo ? -1.0f : distanceSqToBox(retrieveAABB(wmoId), cameraPos);
            queue.push_back(item);
        }
        for (auto &wmoGroupObject: renderPlan->wmoGroupArray.getToLoad()) {
            if (wmoGroupObject == nullptr) continue;
            PostLoadItem item;
            item.kind = PostLoadItem::Kind::WmoGroup;
            item.group = wmoGroupObject.get();
            item.cameraWmo = cameraWmo != nullptr && wmoGroupObject->getWmoApi() == static_cast<IWmoApi *>(cameraWmo);
            item.distanceSq = (wmoGroupObject->getWmoApi() != nullptr) ?
                distanceSqToBox(wmoGroupObject->getWorldAABB(), cameraPos) : 0.0f;
            queue.push_back(item);
        }
    }

    std::sort(queue.begin(), queue.end(), [](const PostLoadItem &a, const PostLoadItem &b) {
        if (a.cameraWmo != b.cameraWmo) return a.cameraWmo;
        if (a.distanceSq != b.distanceSq) return a.distanceSq < b.distanceSq;
        return a.kind < b.kind;
    });

    const auto start = std::chrono::steady_clock::now();
    int finished = 0;
    for (auto &item: queue) {
        bool done = false;
        switch (item.kind) {
            case PostLoadItem::Kind::M2: {
                auto m2Object = m2Factory->getObjectById<0>(item.m2Id);
                if (m2Object == nullptr) continue;
                m2Object->doLoadGeom(sceneRenderer);
                done = m2Object->getGetIsLoaded();
                break;
            }
            case PostLoadItem::Kind::WmoRoot: {
                auto wmoObject = wmoFactory->getObjectById<0>(item.wmoId);
                if (wmoObject == nullptr) continue;
                done = wmoObject->doPostLoad(sceneRenderer);
                break;
            }
            case PostLoadItem::Kind::WmoGroup:
                done = item.group->doPostLoad(sceneRenderer, m_liquidMaterialManager);
                break;
        }

        if (done) finished++;
        if (!fastLoad && finished >= 1 && (std::chrono::steady_clock::now() - start) >= POST_LOAD_BUDGET) break;
    }
    return queue.size();
}

/*
robots@Impl: The fast-load window, the mirror of the full deallocation window after a map change. An initial load, a flight
start, a map open or a portal hop, is a screen the user waits on, so it finishes as fast as the loader delivers: the tile cap
and the finishing budget are lifted, and the budgets return for the rest of the session once the window closes.

SceneWindow::unload opens it ahead of every open, the first of the process included, and stamps a generation. A Map takes the
generation current when it is made and takes part only while it is the latest: the render thread may still run the old map's
last plan after the unload, and that map must neither close the new window nor read it as its own.

It closes when the tiles around the arrival point are in, which is settled, or after FAST_LOAD_CEILING, whichever is first.
Settled is read once per frame, in doPostLoad: the plan asked for at least one tile, no tile it asked for, visible or preloaded,
was still loading when the frame began, and the finishing queue held nothing. The tiles are read ahead of the frame's own tile
pass, so a tile that finished this frame, whose models no plan has listed yet, does not count. A map whose WDT is one WMO has no
tile to read, so it waits for its queue to have held work once and then to be empty. Until the WDT has loaded there is nothing
to tell the two apart, so the map is not settled.

The reading is made every frame, not only while the window is open, and the plan carries it as loadSettled, which the flyover's
`world` line reports. A frame that asks for no tile at all, the camera above the draw distance or the view the far land alone,
has none loading, so it counts as settled once the queue is empty.
*/
namespace {
std::atomic<uint64_t> fastLoadGeneration{0};
std::atomic<bool> fastLoadWindowOpen{false};
std::atomic<long long> fastLoadStartNs{0};

long long steadyNowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Closes the window of `generation`, once, and logs how long it ran and why.
void closeFastLoad(uint64_t generation, const char *reason) {
    if (fastLoadGeneration.load() != generation) return;
    if (!fastLoadWindowOpen.exchange(false)) return;

    const long long ms = (steadyNowNs() - fastLoadStartNs.load()) / 1000000;
    std::cerr << "fast load: window closed after " << ms << " ms, " << reason << std::endl;
}
}

void Map::requestFastLoad() {
    fastLoadStartNs.store(steadyNowNs());
    fastLoadGeneration.fetch_add(1);
    fastLoadWindowOpen.store(true);
}

uint64_t Map::currentFastLoadGeneration() {
    return fastLoadGeneration.load();
}

bool Map::fastLoadOpen() {
    if (m_fastLoadGeneration != fastLoadGeneration.load() || !fastLoadWindowOpen.load()) return false;

    const long long elapsedNs = steadyNowNs() - fastLoadStartNs.load();
    if (elapsedNs >= std::chrono::duration_cast<std::chrono::nanoseconds>(FAST_LOAD_CEILING).count()) {
        closeFastLoad(m_fastLoadGeneration, "ceiling");
        return false;
    }
    return true;
}

bool Map::wdtLoaded() const {
    return m_wdtfile != nullptr && m_wdtfile->getStatus() == FileStatus::FSLoaded;
}

bool Map::loadSettled(bool tilesAsked, bool tilesLoaded, size_t queued) {
    if (!wdtLoaded()) return false;

    bool settled;
    if (m_wdtfile->mphd->flags.wdt_uses_global_map_obj) {
        settled = m_fastLoadSawWork && queued == 0;
        if (queued > 0) m_fastLoadSawWork = true;
    } else {
        settled = tilesAsked && tilesLoaded && queued == 0;
    }
    return settled;
}

void Map::doPostLoad(const HMapSceneBufferCreate &sceneRenderer, const HMapRenderPlan &renderPlan) {
    ZoneScoped;

    //Init LiquidManager
    if (m_liquidMaterialManager == nullptr) {
        m_liquidMaterialManager = std::make_unique<LiquidMaterialManager>(m_api, sceneRenderer);
    }

    m_liquidMaterialManager->update(m_currentTime);

    const bool fastLoad = fastLoadOpen();

    //The tiles as the frame begins, for the fast-load window and a still: a tile that finishes below is read as still loading
    bool tilesAsked = false;
    bool tilesLoaded = true;
    for (auto &adtObject: renderPlan->adtArray) {
        tilesAsked = true;
        tilesLoaded = tilesLoaded && adtObject.adtObject->isLoaded();
    }
    for (auto &adtObject: renderPlan->adtPreload) {
        tilesAsked = true;
        tilesLoaded = tilesLoaded && adtObject->isLoaded();
    }

    {
        ZoneScopedN("Load m2 main");
        if (m_api->getConfig()->renderM2) {
            for (auto &m2ObjectId: renderPlan->m2Array.getToLoadMain()) {
                auto m2Object = m2Factory->getObjectById<0>(m2ObjectId);
                if (m2Object == nullptr) continue;
                m2Object->doLoadMainFile();
            }
        }
    }

    if (m_api->getConfig()->renderSkyDom) {
        if (auto skyboxView = renderPlan->viewsHolder.getSkybox()) {
            for (auto &m2ObjectId: skyboxView->m2List.getToLoadMain()) {
                auto m2Object = m2Factory->getObjectById<0>(m2ObjectId);
                if (m2Object == nullptr) continue;
                m2Object->doLoadMainFile();
            }
            for (auto &m2ObjectId: skyboxView->m2List.getToLoadGeom()) {
                auto m2Object = m2Factory->getObjectById<0>(m2ObjectId);
                if (m2Object == nullptr) continue;
                m2Object->doLoadGeom(sceneRenderer);
            }
            for (auto &m2ObjectId: skyboxView->stars.getToLoadMain()) {
                auto m2Object = m2Factory->getObjectById<0>(m2ObjectId);
                if (m2Object == nullptr) continue;
                m2Object->doLoadMainFile();
            }
            for (auto &m2ObjectId: skyboxView->stars.getToLoadGeom()) {
                auto m2Object = m2Factory->getObjectById<0>(m2ObjectId);
                if (m2Object == nullptr) continue;
                m2Object->doLoadGeom(sceneRenderer);
            }
        }
    }
//    }

    const size_t queued = finishLoadedGeometry(sceneRenderer, renderPlan, fastLoad);

    {
        ZoneScopedN("Home creatures");
        for (auto wmoId: renderPlan->wmoArray.getCandidates()) {
            auto wmoObject = wmoFactory->getObjectById<0>(wmoId);
            if (wmoObject != nullptr) {
                wmoObject->homeCreaturesIfChanged();
            }
        }
        //The WMO the camera stands in is drawn by traversal and is no candidate when no exterior shows through its portals
        if (auto currentWmo = wmoFactory->getObjectById<0>(renderPlan->m_currentWMO)) {
            currentWmo->homeCreaturesIfChanged();
        }
    }

    {
        ZoneScopedN("Load adt");
        int adtProcessed = 0;
        for (auto &adtObject: renderPlan->adtArray) {
            adtProcessed += (adtObject.adtObject->doPostLoad(sceneRenderer, m_liquidMaterialManager)) ? 1 : 0;
            if (!fastLoad && adtProcessed >= MAX_LOAD_ADT_PER_FRAME) break;
        }
        //Preloaded tiles take only what the visible ones left of the per-frame cap, which the fast-load window lifts
        for (auto &adtObject: renderPlan->adtPreload) {
            if (!fastLoad && adtProcessed >= MAX_LOAD_ADT_PER_FRAME) break;
            adtProcessed += (adtObject->doPostLoad(sceneRenderer, m_liquidMaterialManager)) ? 1 : 0;
        }
    }

    const bool settled = loadSettled(tilesAsked, tilesLoaded, queued);
    const bool tilesMap = wdtLoaded() && !m_wdtfile->mphd->flags.wdt_uses_global_map_obj;
    renderPlan->loadSettled = settled || (tilesMap && !tilesAsked && queued == 0);
    if (fastLoad && settled) closeFastLoad(m_fastLoadGeneration, "settled");

    if (skyMesh == nullptr) {
        auto skyMeshBinding = createSkyBindings(sceneRenderer);
        std::tie(skyMesh, skyMeshMat) = createSkyMesh(sceneRenderer, skyMeshBinding, false);
        std::tie(skyMesh0x4Sky, skyMeshMat0x4) = createSkyMesh(sceneRenderer, skyMeshBinding, true);
    }

    if (m_planetMeshes[0] == nullptr) {
        static const std::array<int, 3> planetTextureFdids = {{186220, 4629581, 4629582}}; // sun, moon1, moon2

        auto planetBindings = createPlanetBindings(sceneRenderer);
        for (int i = 0; i < 3; i++) {
            if (m_planetTextures[i] == nullptr) {
                auto texture = m_api->cacheStorage->getTextureCache()->getFileId(planetTextureFdids[i]);
                if (texture != nullptr) {
                    m_planetTextures[i] = m_api->hDevice->createBlpTexture(texture, true, true);
                }
            }
            if (m_planetTextures[i] != nullptr) {
                std::tie(m_planetMeshes[i], m_planetMats[i]) = createPlanetMesh(sceneRenderer, planetBindings, m_planetTextures[i]);
            }
        }
    }
    bool renderAntiPortals = m_api->getConfig()->renderAntiPortals;

    if (renderAntiPortals) {
        if(auto const &exterior = renderPlan->viewsHolder.getExterior()) {
            exterior->produceTransformedPortalMeshes(sceneRenderer, m_api,
                                                     exterior->worldAntiPortalVertices, true);
        }
    }

};

void Map::update(const HMapRenderPlan &renderPlan) {
    ZoneScoped;

    mathfu::vec3 cameraVec3   = renderPlan->renderingMatrices->cameraPos.xyz();
    mathfu::mat4 &frustumMat  = renderPlan->renderingMatrices->perspectiveMat;
    mathfu::mat4 &lookAtMat   = renderPlan->renderingMatrices->lookAtMat;
    animTime_t deltaTime      = renderPlan->deltaTime;

    Config* config = this->m_api->getConfig();

    auto &m2ToDraw = renderPlan->m2Array.getDrawn();
    {
        ZoneScopedN("m2UpdateframeCounter");

        auto threadsAvailable = m_api->getConfig()->hardwareThreadCount();

        int granSize = m2ToDraw.size() / (2 * threadsAvailable);
        if (granSize == 0) granSize = m2ToDraw.size();

        std::mutex fillLights;
        if (granSize > 0) {
            oneapi::tbb::task_arena arena(m_api->getConfig()->hardwareThreadCount(), 1);
            auto processingFrame = FrameContext::getCurrentProcessingFrameNumber();
            arena.execute([&] {
                tbb::affinity_partitioner ap;
                tbb::parallel_for(tbb::blocked_range<size_t>(0, m2ToDraw.size(), granSize),
                    [&](tbb::blocked_range<size_t> r) {
                        FrameContext::setCurrentProcessingFrameNumber(processingFrame);
                        std::vector<LocalLight> localLights;

                        for (size_t i = r.begin(); i != r.end(); ++i) {
                            auto m2Object = m2Factory->getObjectById<0>(m2ToDraw[i]);
                            if (m2Object == nullptr) continue;
                            m2Object->update(deltaTime, cameraVec3, lookAtMat);

                            m2Object->collectLights(localLights);
                        }
                        if (!localLights.empty()) {
                            std::unique_lock<std::mutex> lock(fillLights);
                            renderPlan->pointLights.insert(
                                renderPlan->pointLights.end(),
                                localLights.begin(),localLights.end());
                        }
                    }, ap);
            });
        }

        if (auto skyBoxView = renderPlan->viewsHolder.getSkybox()) {
            for (auto &m2ObjectId : skyBoxView->m2List.getDrawn()) {
                auto m2Object = m2Factory->getObjectById<0>(m2ObjectId);
                if (m2Object == nullptr) continue;
                m2Object->update(deltaTime, cameraVec3, lookAtMat);
            }
            for (auto &m2ObjectId : skyBoxView->stars.getDrawn()) {
                auto m2Object = m2Factory->getObjectById<0>(m2ObjectId);
                if (m2Object == nullptr) continue;
                m2Object->update(deltaTime, cameraVec3, lookAtMat);
            }
        }
    }

    {
        ZoneScopedN("wmoUpdate");

        for (const auto &wmoId: renderPlan->wmoArray.getToDrawn()) {
            auto wmoObject = wmoFactory->getObjectById<0>(wmoId);
            if (wmoObject == nullptr) continue;
            wmoObject->update();
        }
    }

    {
        ZoneScopedN("wmoGroupUpdate");
        for (const auto &wmoGroupObject: renderPlan->wmoGroupArray.getToDraw()) {
            if (wmoGroupObject == nullptr) continue;
            wmoGroupObject->update();
        }
    }

    {
        ZoneScopedN("adtUpdate");
        {
            std::unordered_set<std::shared_ptr<AdtObject>> processedADT;
            for (const auto &adtObjectRes: renderPlan->adtArray) {
                if (processedADT.find(adtObjectRes.adtObject) != processedADT.end()) {
                    adtObjectRes.adtObject->update(deltaTime);
                    processedADT.insert(adtObjectRes.adtObject);
                }
            }
        }
    }

    //2. Calc distance every 100 ms
    {
        ZoneScopedN("Calc m2 distance");
        oneapi::tbb::task_arena arena(m_api->getConfig()->hardwareThreadCount(), 1);
        arena.execute([&] {
            tbb::parallel_for(tbb::blocked_range<size_t>(0, m2ToDraw.size(), 500),
                              [&](tbb::blocked_range<size_t> r) {
                                  for (size_t i = r.begin(); i != r.end(); ++i) {
                                      auto m2Object = m2Factory->getObjectById<0>(m2ToDraw[i]);
                                      if (m2Object == nullptr) continue;
                                      m2Object->calcDistance(cameraVec3);
                                  }
                              }, tbb::auto_partitioner()
            );
        });
    }

    //Cleanup ADT every 10 seconds, one tile a frame
    /*
    robots@Impl: A tile's destructor releases its models, groups, textures and buffers, and every tile past its time-to-live
    went in the same frame, so a camera that had left a district dropped the whole of it at once. The 10 second pass now frees
    the first tile it finds and leaves the others to the frames after it: the pass's own timer is reset only when no candidate
    is left, so while one is, the next frame runs the pass again.
    */
    {
        ZoneScopedN("Cleanup ADT");
        if (adtFreeLambda != nullptr && adtFreeLambda(true, false, this->m_currentTime)) {
            bool freed = false;
            bool candidatesLeft = false;
            for (int i = 0; i < 64 && !candidatesLeft; i++) {
                for (int j = 0; j < 64; j++) {
                    auto adtObj = mapTiles[i][j];
                    //Free obj, if it was unused for 10 secs
                    if (adtObj != nullptr && adtObj->getFreeStrategy()(true, false, this->m_currentTime)) {
                        if (freed) {
                            candidatesLeft = true;
                            break;
                        }
//                    std::cout << "try to free adtObj" << std::endl;

                        mapTiles[i][j] = nullptr;
                        freed = true;
                    }
                }
            }

            if (!candidatesLeft) {
                adtFreeLambda(false, true, this->m_currentTime + deltaTime);
            }
        }
    }

    this->m_currentTime += deltaTime;
}

void Map::updateBuffers(const HMapSceneBufferCreate &sceneRenderer, const HMapRenderPlan &renderPlan) {
    ZoneScoped;
    if (skyMeshMat0x4)
    {
        auto &meshblockVS = skyMeshMat0x4->m_skyColors->getObject();
        auto EndFogColor = !renderPlan->frameDependentData->fogResults.empty() ?
                           renderPlan->frameDependentData->fogResults[0].EndFogColor:
                           mathfu::vec3(0,0,0);

        if (EndFogColor.Length() < 0.0001) {
            EndFogColor = renderPlan->frameDependentData->skyColors.SkyFogColor;
        }


        auto EndFogColorV4_1 = mathfu::vec4(EndFogColor, 0.0);
        auto EndFogColorV4_2 = mathfu::vec4(EndFogColor, 1.0);
        meshblockVS.skyColor[0] = EndFogColorV4_1;
        meshblockVS.skyColor[1] = EndFogColorV4_1;
        meshblockVS.skyColor[2] = EndFogColorV4_1;
        meshblockVS.skyColor[3] = EndFogColorV4_1;
        meshblockVS.skyColor[4] = EndFogColorV4_1;
        meshblockVS.skyColor[5] = EndFogColorV4_2;
        skyMeshMat0x4->m_skyColors->save();
    }
    if (skyMeshMat)
    {
        auto &meshblockVS = skyMeshMat->m_skyColors->getObject();
        meshblockVS.skyColor[0] = mathfu::vec4(renderPlan->frameDependentData->skyColors.SkyTopColor,    0);
        meshblockVS.skyColor[1] = mathfu::vec4(renderPlan->frameDependentData->skyColors.SkyMiddleColor, 0);
        meshblockVS.skyColor[2] = mathfu::vec4(renderPlan->frameDependentData->skyColors.SkyBand1Color,  0);
        meshblockVS.skyColor[3] = mathfu::vec4(renderPlan->frameDependentData->skyColors.SkyBand2Color,  0);
        meshblockVS.skyColor[4] = mathfu::vec4(renderPlan->frameDependentData->skyColors.SkySmogColor,   0);
        meshblockVS.skyColor[5] = mathfu::vec4(renderPlan->frameDependentData->skyColors.SkyFogColor,    0);
        skyMeshMat->m_skyColors->save();
    }

    // Planet billboards: upload world pos/scale and color/alpha computed by DayNightLightHolder
    for (int i = 0; i < 3; i++) {
        if (m_planetMats[i] == nullptr) continue;

        const auto &planetData = renderPlan->frameDependentData->planets[i];
        auto &blockVS = m_planetMats[i]->m_planetData->getObject();
        blockVS.uWorldPosAndScale = mathfu::vec4(planetData.worldPos, planetData.scale);
        blockVS.uColorAndAlpha = mathfu::vec4(planetData.color, planetData.alpha);
        blockVS.uCamPos = mathfu::vec4(renderPlan->renderingMatrices->cameraPos.xyz(), 0.0f);
        m_planetMats[i]->m_planetData->save();
    }

    {
        ZoneScopedN("m2BuffersUpdate");
        auto threadsAvailable = m_api->getConfig()->hardwareThreadCount();
        auto &m2ToDraw = renderPlan->m2Array.getDrawn();
        int granSize = m2ToDraw.size() / (2 * threadsAvailable);
        if (granSize == 0) granSize = m2ToDraw.size();

        //Can't be paralleled?
        {
            ZoneScopedN("fitParticleAndRibbonBuffersToSize");
            auto &drawnM2s = renderPlan->m2Array.getDrawn();
            for (auto &m2ObjectId: drawnM2s) {
                auto m2Object = m2Factory->getObjectById<0>(m2ObjectId);
                if (m2Object != nullptr) {
                    m2Object->fitParticleAndRibbonBuffersToSize(sceneRenderer);
                }
            }
        }

        auto updateLambda = [&](const std::function<void(M2Object *)> &callback) {
            if (granSize > 0) {
                auto l_device = m_api->hDevice;
                auto processingFrame = FrameContext::getCurrentProcessingFrameNumber();
                tbb::affinity_partitioner ap;
                tbb::parallel_for(tbb::blocked_range<size_t>(0, m2ToDraw.size(), granSize),
                    [&](tbb::blocked_range<size_t> r) {
                        FrameContext::setCurrentProcessingFrameNumber(processingFrame);
                        for (size_t i = r.begin(); i != r.end(); ++i) {
                            auto m2Object = m2Factory->getObjectById<0>(m2ToDraw[i]);
                            if (m2Object != nullptr) {
                                callback(m2Object);
                            }
                        }
                    }, ap);
            }
        };

        {
            ZoneScopedN("upload Model buffers");

            updateLambda([&renderPlan](M2Object *m2Object) {
                m2Object->uploadBuffers(renderPlan->renderingMatrices->lookAtMat,
                                                 renderPlan->frameDependentData);
            });
        }

        {
            ZoneScopedN("upload generators buffers");

            updateLambda([&renderPlan](M2Object *m2Object) {
                m2Object->uploadGeneratorBuffers(renderPlan->renderingMatrices->lookAtMat,
                                                 renderPlan->frameDependentData);
            });
        }



//        for (auto &m2ObjectId: renderPlan->m2Array.getDrawn()) {
//            auto m2Object = m2Factory->getObjectById<0>(m2ObjectId);
//            if (m2Object != nullptr) {
//                m2Object->uploadGeneratorBuffers(renderPlan->renderingMatrices->lookAtMat,
//                                                 renderPlan->frameDependentData);
//            }
//        }
    }
    {
        ZoneScopedN("m2SkyboxBuffersUpdate");
        if (auto skyBoxView = renderPlan->viewsHolder.getSkybox()) {
            for (auto &m2ObjectId: skyBoxView->m2List.getDrawn()) {
                auto m2Object = m2Factory->getObjectById<0>(m2ObjectId);
                m2Object->fitParticleAndRibbonBuffersToSize(sceneRenderer);
            }
            for (auto &m2ObjectId: skyBoxView->stars.getDrawn()) {
                auto m2Object = m2Factory->getObjectById<0>(m2ObjectId);
                m2Object->fitParticleAndRibbonBuffersToSize(sceneRenderer);
            }
        }
    }
    {
        ZoneScopedN("m2SkyboxBuffersUpdate");
        if (auto skyBoxView = renderPlan->viewsHolder.getSkybox()) {
            for (auto &m2ObjectId: skyBoxView->m2List.getDrawn()) {
                auto m2Object = m2Factory->getObjectById<0>(m2ObjectId);
                m2Object->uploadBuffers(renderPlan->renderingMatrices->lookAtMat,
                                                 renderPlan->frameDependentData);
            }
            for (auto &m2ObjectId: skyBoxView->stars.getDrawn()) {
                auto m2Object = m2Factory->getObjectById<0>(m2ObjectId);
                m2Object->uploadBuffers(renderPlan->renderingMatrices->lookAtMat,
                                                 renderPlan->frameDependentData);
            }
        }
    }
    {
        ZoneScopedN("m2SkyboxGenerateBuffersUpdate");
        if (auto skyBoxView = renderPlan->viewsHolder.getSkybox()) {
            for (auto &m2ObjectId: skyBoxView->m2List.getDrawn()) {
                auto m2Object = m2Factory->getObjectById<0>(m2ObjectId);
                m2Object->uploadGeneratorBuffers(renderPlan->renderingMatrices->lookAtMat,
                                                 renderPlan->frameDependentData);
            }
            for (auto &m2ObjectId: skyBoxView->stars.getDrawn()) {
                auto m2Object = m2Factory->getObjectById<0>(m2ObjectId);
                m2Object->uploadGeneratorBuffers(renderPlan->renderingMatrices->lookAtMat,
                                                 renderPlan->frameDependentData);
            }
        }
    }

    {
        ZoneScopedN("wmoBuffersUpdate");
        for (auto &wmoGroupObject: renderPlan->wmoGroupArray.getToDraw()) {
            if (wmoGroupObject == nullptr) continue;
            wmoGroupObject->uploadGeneratorBuffers(renderPlan->frameDependentData, getCurrentSceneTime());
        }
    }

    {
        ZoneScopedN("adtBuffersUpdate");
        std::unordered_set<std::shared_ptr<AdtObject>> processedADT;
        for (const auto &adtObjectRes: renderPlan->adtArray) {
            if (processedADT.find(adtObjectRes.adtObject) != processedADT.end()) {
                adtObjectRes.adtObject->uploadGeneratorBuffers(renderPlan->frameDependentData);
                processedADT.insert(adtObjectRes.adtObject);
            }
        }
    }
}

std::shared_ptr<M2Object> Map::getM2Object(std::string fileName, const SMDoodadDef &doodadDef) {
    auto it = m_m2MapObjects[doodadDef.uniqueId];
    if (auto m2Object = it.lock()) {
        return m2Object;
    } else {
        m2Object = m2Factory->createObject(m_api);
        m2Object->setLoadParams(0, {}, {});
        m2Object->setModelFileName(fileName);
        m2Object->createPlacementMatrix(doodadDef);
        m2Object->calcWorldPosition();

        m_m2MapObjects[doodadDef.uniqueId] = std::weak_ptr<M2Object>(m2Object);
        return m2Object;
    }
    return nullptr;
}


std::shared_ptr<M2Object> Map::getM2Object(int fileDataId, const SMDoodadDef &doodadDef) {
    auto it = m_m2MapObjects[doodadDef.uniqueId];
    if (auto m2Object = it.lock()) {
        return m2Object;
    } else {
        m2Object = m2Factory->createObject(m_api);
        m2Object->setLoadParams(0, {}, {});
        m2Object->setModelFileId(fileDataId);
        m2Object->createPlacementMatrix(doodadDef);
        m2Object->calcWorldPosition();

        m_m2MapObjects[doodadDef.uniqueId] = std::weak_ptr<M2Object>(m2Object);
        return m2Object;
    }
    return nullptr;
}
std::shared_ptr<WmoObject> Map::getWmoObject(std::string fileName, const SMMapObjDef &mapObjDef, const PointerChecker<MWDR> &MWDR, const PointerChecker<uint16_t> &MWDS) {
    auto it = m_wmoMapObjects[mapObjDef.uniqueId];
    if (auto wmoObject = it.lock()) {
        return wmoObject;
    } else {
        wmoObject = wmoFactory->createObject(m_api);
        wmoObject->setLoadingParam(mapObjDef, MWDR, MWDS);
        wmoObject->setModelFileName(fileName);

        m_wmoMapObjects[mapObjDef.uniqueId] = std::weak_ptr<WmoObject>(wmoObject);
        return wmoObject;
    }
    return nullptr;
}

std::shared_ptr<WmoObject> Map::getWmoObject(int fileDataId, const SMMapObjDef &mapObjDef, const PointerChecker<MWDR> &MWDR, const PointerChecker<uint16_t> &MWDS) {
    auto it = m_wmoMapObjects[mapObjDef.uniqueId];
    if (auto wmoObject = it.lock()) {
        return wmoObject;
    } else {
        wmoObject = wmoFactory->createObject(m_api);
        wmoObject->setLoadingParam(mapObjDef, MWDR, MWDS);
        wmoObject->setModelFileId(fileDataId);

        m_wmoMapObjects[mapObjDef.uniqueId] = std::weak_ptr<WmoObject>(wmoObject);
        return wmoObject;
    }
    return nullptr;
}

std::shared_ptr<WmoObject> Map::getWmoObject(std::string fileName, const SMMapObjDefObj1 &mapObjDef, const PointerChecker<MWDR> &MWDR, const PointerChecker<uint16_t> &MWDS) {
    auto it = m_wmoMapObjects[mapObjDef.uniqueId];
    if (auto wmoObject = it.lock()) {
        return wmoObject;
    } else {
        wmoObject = wmoFactory->createObject(m_api);
        wmoObject->setLoadingParam(mapObjDef, MWDR, MWDS);
        wmoObject->setModelFileName(fileName);

        m_wmoMapObjects[mapObjDef.uniqueId] = std::weak_ptr<WmoObject>(wmoObject);
        return wmoObject;
    }
    return nullptr;
}

std::shared_ptr<WmoObject> Map::getWmoObject(int fileDataId, const SMMapObjDefObj1 &mapObjDef, const PointerChecker<MWDR> &MWDR, const PointerChecker<uint16_t> &MWDS) {
    auto it = m_wmoMapObjects[mapObjDef.uniqueId];
    if (auto wmoObject = it.lock()) {
        return wmoObject;
    } else {
        wmoObject = wmoFactory->createObject(m_api);
        wmoObject->setLoadingParam(mapObjDef, MWDR, MWDS);
        wmoObject->setModelFileId(fileDataId);

        m_wmoMapObjects[mapObjDef.uniqueId] = std::weak_ptr<WmoObject>(wmoObject);
        return wmoObject;
    }
    return nullptr;
}
animTime_t Map::getCurrentSceneTime() {
    return m_currentTime;
}
mathfu::vec3 Map::getGlobalOffset() {
    return mathfu::vec3(0, 0, 0);
}




