#undef _WIN32_WINNT		// Allow use of features specific to Windows XP or later.                   
#define _WIN32_WINNT 0x0502

#include <GodziWebControl/MapControl>
#include <GodziWebControl/NodeMasks>
#include <GodziWebControl/Utils>
#include <GodziWebControl/MapCommands>
#include <GodziWebControl/GetObjectInfoCommand>
#include <GodziWebControl/MapEventHandler>
#include <GodziWebControl/Annotations>
#include <GodziWebControl/Kml>
//#include <GodziWebControl/FirstPersonManipulator>

#include <osgEarth/Registry>
#include <osgEarth/Cache>
#include <osgEarthUtil/Controls>
#include <osgEarthUtil/EarthManipulator>
#include <osgEarthUtil/AutoClipPlaneHandler>
#include <osgEarthDrivers/cache_filesystem/FileSystemCache>

#include <osg/io_utils>
#include <osg/Version>
#include <osg/DisplaySettings>
#include <osg/GLObjects>
#include <osgDB/ReadFile>
#include <osgDB/FileUtils>
#include <osgDB/FileNameUtils>
#include <osgGA/TrackballManipulator>
#include <osgGA/FlightManipulator>
#include <osgGA/DriveManipulator>
#include <osgGA/KeySwitchMatrixManipulator>
#include <osgGA/TerrainManipulator>
#include <osgGA/SphericalManipulator>
#include <osgViewer/api/Win32/GraphicsWindowWin32>
#include <OpenThreads/Thread>

#include <sstream>
#include <iostream>

#include <windows.h>
#include <windowsx.h>

#include <string.h>
#include <atlconv.h>


//http://www.codeproject.com/KB/DLL/DLLModuleFileName.aspx
EXTERN_C IMAGE_DOS_HEADER __ImageBase;

using namespace GodziWebControl;
using namespace osgEarth::Util::Controls;

#define WM_GODZI_EVENT (WM_USER + 0)

class EventData
{
public:
    EventData(const std::string &target, const std::string &eventName, const std::string &data):
      _target(target),
          _event(eventName),
          _data(data)
      {
      }
      std::string _data;
      std::string _target;
      std::string _event;
};


class MapInit
{
public:
    MapInit():
      _log(0)
      {

          //Redirect cout and cerr to the log file
          char *appData = getenv("APPDATA");

          //Only enable logging if an env var is set.
          if (getenv("GODZI_LOGGING") != 0)
          {
              std::string logPath = std::string(appData) + "\\GodziWebControl\\";
              bool gotFilename = false;         
              std::string filename;
              int i =0; 
              while (!gotFilename)
              {
                  std::stringstream ss;
                  ss << logPath << "godzi_log" << i << ".txt" << std::flush;
                  filename = ss.str();
                  gotFilename = !osgDB::fileExists(filename);
                  i++;
              }
              _log = new std::ofstream( filename.c_str() );
              std::cout.rdbuf( _log->rdbuf() );
              std::cerr.rdbuf( _log->rdbuf() );

              std::string level( getenv("GODZI_LOGGING") );
              if ( level == "INFO" )
                  osgEarth::setNotifyLevel( osg::INFO );
              else if ( level == "DEBUG" )
                  osgEarth::setNotifyLevel( osg::DEBUG_INFO );
          }



		  std::string cachePath = std::string(appData) + "\\GodziWebControl\\cache";
		  osgDB::makeDirectory( cachePath );
		  //Set up a global cache for all maps to use

          osgEarth::Drivers::FileSystemCacheOptions cacheOpt;
          cacheOpt.rootPath() = cachePath;
          osgEarth::Cache* globalCache = osgEarth::CacheFactory::create(cacheOpt);

		  osgEarth::Registry::instance()->setCache( globalCache );

#if (OPENSCENEGRAPH_MAJOR_VERSION == 2 && OPENSCENEGRAPH_MINOR_VERSION >= 8)
          int numThreads = OpenThreads::GetNumberOfProcessors();
          osg::notify(osg::NOTICE) << "Setting number of database threads to " << numThreads << std::endl;
          osg::DisplaySettings::instance()->setNumOfDatabaseThreadsHint( numThreads );
#endif


          LPCH lpszCurDir = new CHAR[_MAX_PATH];
          ::GetModuleFileNameA((HINSTANCE)&__ImageBase, lpszCurDir, _MAX_PATH);

          char* lastSlash = strrchr(lpszCurDir, '\\');
          if (lastSlash)
              *(lastSlash + 1) = '\0'; 
          osgDB::getLibraryFilePathList().push_front(std::string(lpszCurDir));
          SetDllDirectoryA(lpszCurDir);
      }

      ~MapInit()
      {

          osgEarth::Registry::instance()->setCache( 0L );
          if (_log)
          {
              _log->close();
              delete _log;
          }
      }

      std::ofstream* _log;
};

static MapInit s_mapInit;


static LRESULT CALLBACK WinProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    MapControl* map = (MapControl*)GetWindowLong(hWnd, GWL_USERDATA);
    return map->handleNativeWindowingEvent(hWnd, msg, wParam, lParam);
}

struct MapControlManipLabelHandler : public osgGA::GUIEventHandler
{
    MapControlManipLabelHandler( LabelControl* label )
        : _label(label) { }

    bool handle( const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter& aa )
    {
        if ( ea.getEventType() == ea.KEYDOWN )
        {
            if ( ea.getKey() == '2' )
            {
              _label->setText("First-Person Mode");
            }
            else if ( ea.getKey() == '1' )
            {
              _label->setText("Global Mode");
            }
        }

        return false;
    }

    LabelControl* _label;
};

MapControl::MapControl():
_eventCallback(0)
{
    _commandQueue = new CommandQueue;

    addCommandFactory(new SetMapCommand::Factory());
    addCommandFactory(new ShowSkyCommand::Factory());
    addCommandFactory(new SetSkyDateTimeCommand::Factory());
    addCommandFactory(new GetBackColorCommand::Factory());
    addCommandFactory(new SetBackColorCommand::Factory());
    addCommandFactory(new ChangeVisibilityCommand::Factory());
    addCommandFactory(new GetViewpointCommand::Factory());
    addCommandFactory(new SetViewpointCommand::Factory());
    addCommandFactory(new ToggleStatsCommand::Factory());
    addCommandFactory(new CyclePolygonModeCommand::Factory());
    addCommandFactory(new HomeCommand::Factory());
    addCommandFactory(new FadeMapCommand::Factory());
    addCommandFactory(new ToggleNavDisplayCommand::Factory());

    addCommandFactory(new UpdateIconCommand::Factory());
    addCommandFactory(new RemoveIconCommand::Factory());

    addCommandFactory(new UpdateModelCommand::Factory());
    addCommandFactory(new RemoveModelCommand::Factory());

    addCommandFactory(new UpdateImageLayerCommand::Factory());
    addCommandFactory(new MoveImageLayerCommand::Factory());
    addCommandFactory(new RemoveImageLayerCommand::Factory());
    addCommandFactory(new GetImageLayersCommand::Factory());
    addCommandFactory(new GetImageLayerPropertiesCommand::Factory());

    addCommandFactory(new AddElevationLayerCommand::Factory());
    addCommandFactory(new MoveElevationLayerCommand::Factory());
    addCommandFactory(new RemoveElevationLayerCommand::Factory());
    addCommandFactory(new GetElevationLayersCommand::Factory());
    addCommandFactory(new ToggleElevationLayerCommand::Factory());
    addCommandFactory(new GetElevationLayerPropertiesCommand::Factory());

    addCommandFactory(new AddModelLayerCommand::Factory());
    addCommandFactory(new MoveModelLayerCommand::Factory());
    addCommandFactory(new RemoveModelLayerCommand::Factory());
    addCommandFactory(new GetModelLayersCommand::Factory());
    addCommandFactory(new ToggleModelLayerCommand::Factory());
    addCommandFactory(new GetModelLayerBoundsCommand::Factory());
    addCommandFactory(new GetModelLayerPropertiesCommand::Factory());

    addCommandFactory(new UpdateTextLabelCommand::Factory());
    addCommandFactory(new RemoveTextLabelCommand::Factory());

    addCommandFactory(new GetNamesCommand::Factory());
    addCommandFactory(new GetIntersectionCommand::Factory());   
    addCommandFactory(new GetDescriptionsCommand::Factory());

    addCommandFactory(new GetObjectInfoCommand::Factory());

    addCommandFactory(new SelectionCommandFactory() );

    addCommandFactory(new LoadKmlCommand::Factory());
    addCommandFactory(new RemoveKmlCommand::Factory());

    AnnotationCommands::registerAll(this);
}

void MapControl::init(void* window)
{
    // detect the number of cores on the machine, and allocate pager threads accordingly.
    unsigned cores = OpenThreads::GetNumberOfProcessors();
    unsigned totalThreads = 1, remoteThreads = 1;
    if ( cores > 2 )
    {
        totalThreads = cores;
        remoteThreads = cores/2;
    }
    osg::DisplaySettings::instance()->setNumOfDatabaseThreadsHint( totalThreads );
    osg::DisplaySettings::instance()->setNumOfHttpDatabaseThreadsHint( remoteThreads );


    //Create a new viewer
    _viewer = new osgViewer::Viewer;
    //_viewer->setThreadingModel(osgViewer::ViewerBase::SingleThreaded);

    // enable "on demand" rendering.
    _viewer->setRunFrameScheme( osgViewer::ViewerBase::ON_DEMAND );

    _viewer->setKeyEventSetsDone(0);

    _hwnd = (HWND)window;

    osg::ref_ptr<osg::GraphicsContext::Traits> t = new osg::GraphicsContext::Traits();
    t->inheritedWindowData = new osgViewer::GraphicsWindowWin32::WindowData( _hwnd );
    t->setInheritedWindowPixelFormat = true;
    t->doubleBuffer = true;
    t->windowDecoration = false;
    t->sharedContext = NULL;
    t->supportsResize = false;

    RECT rect;
    ::GetWindowRect( (HWND)_hwnd, &rect );
    t->x = 0;
    t->y = 0;
    t->width = rect.right - rect.left;
    t->height = rect.bottom - rect.top;

    if (t->width == 0) t->width = 1;
    if (t->height == 0) t->height = 1;

    osg::notify(osg::NOTICE) << "Creating window " << t->x << ", " << t->y << ", " << t->width << ", " << t->height << std::endl;

    osg::ref_ptr<osg::GraphicsContext> gc = osg::GraphicsContext::createGraphicsContext( t.get() );  

    //There is an issue somewhere in OSG that has problems with the reuse of context ID's.  To reproduce:
    //1)Open up the main example
    //2)Switch to a different map using the radio buttons
    //3)Open a new, empty tab
    //4)Close the original map tab
    //5)Open a new example tab.  The new viewer created tries to reuse the original context ID and either crashes or textures,geometry become wacky 

    //Doing this prevents context ID reuse
    osg::GraphicsContext::incrementContextIDUsageCount(gc->getState()->getContextID());

    _viewer->getCamera()->setGraphicsContext( gc.get() );
    _viewer->getCamera()->setViewport( new osg::Viewport( 0, 0, t->width, t->height ) );
    _viewer->getCamera()->setProjectionMatrixAsPerspective(30.0f, (double)(t->width)/(double)(t->height), 1.0f, 10000.0f);
    _viewer->getCamera()->setClearColor(_clearColor);

    _root = new osg::Group;

	  osgEarth::Util::EarthManipulator* manip = new osgEarth::Util::EarthManipulator;
    //manip->setSmoothingEnabled(false);
    manip->setIntersectTraversalMask(TERRAIN);
    _viewer->setCameraManipulator( manip );

    //_root->addChild(osgDB::readNodeFile("c:/dev/OpenSceneGraph-Data/cow.osg"));

    /*//Set up a skylight rather than using the traditional headlamp
    _viewer->setLightingMode(osg::View::SKY_LIGHT);
    osg::Light* light = _viewer->getLight();
    light->setAmbient( osg::Vec4( .4, .4, .4, 1 ) );
    light->setDiffuse( osg::Vec4( 1, 1, .8, 1 ) );*/
    //_root->getOrCreateStateSet()->setMode(GL_LIGHTING , osg::StateAttribute::OFF | osg::StateAttribute::OVERRIDE);

    //Create a control canvas and label control to display manipulator mode
    ControlCanvas* cs = ControlCanvas::get( _viewer );
    _manipLabel = new LabelControl( "Global Mode" );
    _manipLabel->setFont( osgText::readFontFile( "arialbd.ttf" ) );
    _manipLabel->setFontSize( 12.0f );
    _manipLabel->setHorizAlign( Control::ALIGN_RIGHT );
    _manipLabel->setVertAlign( Control::ALIGN_BOTTOM );
    _manipLabel->setMargin( 5 );
    _manipLabel->setPadding( 3.0 );
    _manipLabel->setForeColor( 1.0f, 1.0f, 1.0f, 1.0f );
    _manipLabel->setBackColor( 0.2f, 0.2f, 0.2f, 0.3f );
    _manipLabel->setVisible(false);
    cs->addControl( _manipLabel.get() );
    _root->addChild( cs );
    
    _viewer->setSceneData( _root.get() );

    _statsHandler = new osgViewer::StatsHandler();
    _viewer->addEventHandler( _statsHandler.get() );
    _viewer->addEventHandler( new MapEventHandler(this) );

    _stateSetManipulator = new osgGA::StateSetManipulator(_viewer->getCamera()->getOrCreateStateSet());
    _stateSetManipulator->setKeyEventToggleTexturing('x');
    _viewer->addEventHandler( _stateSetManipulator.get() );

    _viewer->setDone( false );

    _previousWindowProcedure = SubclassWindow(_hwnd, (WNDPROC)WinProc);

	SetWindowLong(_hwnd, GWL_USERDATA, (LONG)this);

	_viewer->realize();

    _selectionSet = new SelectionSet( _viewer->getCamera() );

    _viewer->home();


    //Initialize the map callback
    _mapCallback = new MapControlMapCallback(this);
}

void MapControl::run()
{
    if (_viewer.valid())
    {
        while (!_viewer->done())
        {
            bool frameNeeded =
                _paintRequested ||
                _refreshRequested ||
                _viewer->getRunFrameScheme() != osgViewer::ViewerBase::ON_DEMAND ||
                !_commandQueue->empty() ||
                _viewer->checkNeedToDoFrame();

            if ( frameNeeded )
            {
                //Execute any pending commands
                _commandQueue->execute(this);

                //Position the light to be at the eye point
                //osg::Vec3d eye, center, up;
                //_viewer->getCamera()->getViewMatrixAsLookAt(eye, center, up);
                //_viewer->getLight()->setPosition(osg::Vec4(eye, 1));
                //osg::notify(osg::NOTICE) << "Eye " << eye << std::endl;
                _viewer->frame();

                _paintRequested = false;
                _refreshRequested = false;
            }
            microSleep(10);
        }
    }
}

int MapControl::cancel()
{
    int contextID = _viewer->getCamera()->getGraphicsContext()->getState()->getContextID();

    int result = 0;

    osg::notify(osg::NOTICE) << "Doing MapControl::cancel " << std::endl;

    if (isRunning())
    {
        _viewer->setDone( true);
        while (isRunning())
        {
            OpenThreads::Thread::YieldCurrentThread();
            osg::notify(osg::NOTICE) << "  Waiting for thread to cancel " << std::endl;
        }
    }

    //Subclass it back
    SubclassWindow(_hwnd, _previousWindowProcedure);

    //osg::discardAllDeletedGLObjects(contextID);
    //osg::Texture::discardAllDeletedTextureObjects(contextID);

    //Delete the viewer
    _viewer = 0;

    osg::notify(osg::NOTICE) << "  Thread cancelled" << std::endl;
    return result;
}

osgEarth::Util::EarthManipulator* MapControl::selectEarthManipulator()
{
    osgGA::KeySwitchMatrixManipulator* ksm = dynamic_cast<osgGA::KeySwitchMatrixManipulator*>(_viewer->getCameraManipulator());
    if (ksm)
    {
        ksm->selectMatrixManipulator(0);
        _manipLabel->setText("Global Mode");
        return dynamic_cast<osgEarth::Util::EarthManipulator*>(ksm->getMatrixManipulatorWithIndex(0));
    }
}

/*
GodziWebControl::FirstPersonManipulator* MapControl::selectFirstPersonManipulator()
{
    osgGA::KeySwitchMatrixManipulator* ksm = dynamic_cast<osgGA::KeySwitchMatrixManipulator*>(_viewer->getCameraManipulator());
    if (ksm)
    {
        ksm->selectMatrixManipulator(1);
        _manipLabel->setText("First-Person Mode");
        return dynamic_cast<GodziWebControl::FirstPersonManipulator*>(ksm->getMatrixManipulatorWithIndex(1));
    }
}
*/

void MapControl::toggleStats()
{
    int key = _statsHandler->getKeyEventTogglesOnScreenStats();
    _viewer->getEventQueue()->keyPress(key, _viewer->getEventQueue()->getTime());
}

void MapControl::cyclePolygonMode()
{
    int key = _stateSetManipulator->getKeyEventCyclePolygonMode();
    _viewer->getEventQueue()->keyPress( key, _viewer->getEventQueue()->getTime() );
}

float MapControl::getVerticalScale()
{
    if (_terrain.valid())
    {
        return _terrain->getVerticalScale();
    }
    return -1.0f;
}

void MapControl::setVerticalScale(float verticalScale)
{
    if (_terrain.valid())
    {
        _terrain->setVerticalScale(verticalScale);
    }
}

float MapControl::getSampleRatio()
{
    if (_terrain.valid())
    {
        return _terrain->getSampleRatio();
    }
    return -1.0f;
}

void MapControl::setSampleRatio(float sampleRatio)
{
    if (_terrain.valid())
    {
        _terrain->setSampleRatio(sampleRatio);
    }
}



void MapControl::setMapFile(const std::string &mapFile)
{
    osg::notify(osg::NOTICE) << "MapControl::setMapFile " << mapFile << std::endl;

    bool wasMapValid = _mapNode.valid();
    if (_mapNode.valid())
    {
        _mapNode->getMap()->removeMapCallback(_mapCallback);
        //Remove the current map node
		    _root->removeChild( _mapNode.get() );
        
        if (_skyNode.valid())
          _root->removeChild( _skyNode.get() );

        _mapNode->releaseGLObjects();
        osg::flushAllDeletedGLObjects(_viewer->getCamera()->getGraphicsContext()->getState()->getContextID());
        _mapNode = 0;
        _csn = new osg::CoordinateSystemNode();
        _csn->setEllipsoidModel(new osg::EllipsoidModel());
        _terrain = 0;
    }

    //_viewer->getDatabasePager()->clear();

    //If the mapfile is pointing to a URL, prepend a "server:" to allow the URL to pass through to the Earth plugin rather
    //than being downloaded by the CURL plugin.
    std::string filename = mapFile;
    /*if (osgDB::containsServerAddress(filename))
    {
        filename = "server:" + filename;
    }*/


    //Read the file and add it to the scene
    osg::ref_ptr<osg::Node> loaded = osgDB::readNodeFile( filename );
    if ( loaded )
        _mapNode = osgEarth::MapNode::findMapNode( loaded.get() );

    if (_mapNode.valid())
    {
        _mapNode->setName( "map" );
        _mapNode->setNodeMask( TERRAIN );

        osg::notify(osg::NOTICE) << "Warning:  Couldn't find CSN" << std::endl;
        _csn = findTopMostNodeOfType<osg::CoordinateSystemNode>(_mapNode.get());
        if (_csn.valid())
        {
            osg::notify(osg::NOTICE) << "Found CSN using visitor " << std::endl;
        }
        else
        {
            osg::notify(osg::NOTICE) << "Warning:  Couldn't find CSN" << std::endl;
        }

        _terrain = findTopMostNodeOfType<osgTerrain::Terrain>(_mapNode);
        osgEarth::MapNode* mapNode = findTopMostNodeOfType<osgEarth::MapNode>(_mapNode);
        if (mapNode)
        {
            _root->addChild(_mapNode.get());

			      for (unsigned int i = 0; i < mapNode->getMap()->getNumImageLayers(); ++i)
            {
				        mapNode->getMap()->getImageLayerAt(i)->setOpacity( 1.0f );
            }
            if (_skyNode.valid())
            {
              _skyNode = 0L;
              showSkyNode();
            }
        }

        mapNode->getMap()->addMapCallback(_mapCallback);
    }
    _viewer->getDatabasePager()->registerPagedLODs(_root.get());
    _viewer->computeActiveCoordinateSystemNodePath();

    
    osgEarth::Util::EarthManipulator* manip = new osgEarth::Util::EarthManipulator();
    manip->setIntersectTraversalMask( TERRAIN );
    manip->getSettings()->setMinMaxDistance(2.5, DBL_MAX);  // Set min distance to help prevent zooming "into" the ground

    // Setup key switch manip
    osg::ref_ptr<osgGA::KeySwitchMatrixManipulator> keyswitchManipulator = new osgGA::KeySwitchMatrixManipulator;
    keyswitchManipulator->addMatrixManipulator( '1', "Earth", manip );
    //keyswitchManipulator->addMatrixManipulator( '2', "FirstPerson", new GodziWebControl::FirstPersonManipulator() );
    _viewer->setCameraManipulator(keyswitchManipulator.get());

    getView()->addEventHandler(new MapControlManipLabelHandler(_manipLabel));

    if (_mapNode.valid())
    {
        // Attempting to prevent zooming "into" the ground
        _viewer->getCamera()->setNearFarRatio(0.00002);        

        // install the Feature Manipulation tool.
        _manipTool = new osgEarth::Util::FeatureManipTool( _mapNode );
        _viewer->addEventHandler( _manipTool );
    }

    //Go home if we are going from an invalid map to a valid one
    if (!wasMapValid && _mapNode.valid())
    {
        _viewer->home();
    }
}

void MapControl::showSkyNode()
{
  if (!_skyNode.valid())
  {
    _skyNode = new osgEarth::Util::SkyNode( _mapNode->getMap() );
    _skyNode->setDateTime( 2011, 3, 6, 12.0 );
    _skyNode->attach( _viewer );
    _root->addChild( _skyNode );
  }
}

void MapControl::setSkyDateTime(int year, int month, int day, double timeUTC)
{
  if (_skyNode.valid())
    _skyNode->setDateTime(year, month, day, timeUTC);
}

void MapControl::toggleNavDisplay(bool visible)
{
  if (_manipLabel.valid())
  {
    _manipLabel->setVisible(visible);
    //_viewer->requestRedraw();
    //requestRefresh();
  }
}

LRESULT MapControl::handleNativeWindowingEvent( HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam )
{
    switch (msg) {
        case WM_GODZI_EVENT:
            {
                EventData *ed = (EventData*)wParam;
                if (ed)
                {
                    if (_eventCallback) _eventCallback->handleEvent(ed->_target, ed->_event, ed->_data);
                    delete ed;
                }
                return 0;
            }
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
            {
                ::SetFocus ( hwnd );
                break;
            }
        case WM_MOUSEWHEEL:
            {
                getView()->getEventQueue()->mouseScroll(GET_WHEEL_DELTA_WPARAM(wParam)<0 ? osgGA::GUIEventAdapter::SCROLL_DOWN : osgGA::GUIEventAdapter::SCROLL_UP);
                return 0;
            }
        case WM_PAINT:
            {
                // this fires whenever part of the window needs repainting (resize, min/maximize, unobscured)
                _paintRequested = true;
                ValidateRect(hwnd, NULL);
                break;
            }
    }
    return ::CallWindowProc(_previousWindowProcedure, hwnd, msg, wParam, lParam);
}

void MapControl::latLongHeightToXYZ(double lat, double lon, double height, double &x, double &y, double &z)
{
    //TODO:  Rework so that we are using the CSN again.  Some of the elements like GeoIcons expect the map to always have a consistent CSN
    /*if (_csn.valid() && _csn->getEllipsoidModel())
    {
        _csn->getEllipsoidModel()->convertLatLongHeightToXYZ(osg::DegreesToRadians(lat), osg::DegreesToRadians(lon), height, x, y, z);
    }
    else
    {
        x = lon;
        y = lat;
        z = height;
    }*/

    //osg::ref_ptr<osg::EllipsoidModel> em = new osg::EllipsoidModel();
    //em->convertLatLongHeightToXYZ(osg::DegreesToRadians(lat), osg::DegreesToRadians(lon), height, x, y, z);

    _mapNode->getMap()->getProfile()->getSRS()->getEllipsoid()->convertLatLongHeightToXYZ(
        osg::DegreesToRadians(lat), osg::DegreesToRadians(lon), height, x, y, z);
}

void MapControl::xyzToLatLongHeight(double x, double y, double z, double &lat, double &lon, double &height)
{
    _mapNode->getMap()->getProfile()->getSRS()->getEllipsoid()->convertXYZToLatLongHeight(
        x, y, z, lat, lon, height );
    lon = osg::RadiansToDegrees( lon );
    lat = osg::RadiansToDegrees( lat );

    //TODO:  Rework so that we are using the CSN again.  Some of the elements like GeoIcons expect the map to always have a consistent CSN
    //osg::ref_ptr<osg::EllipsoidModel> em = new osg::EllipsoidModel();
    //em->convertXYZToLatLongHeight(x, y, z, lat, lon, height);
    //lon = osg::RadiansToDegrees(lon);
    //lat = osg::RadiansToDegrees(lat);
    /*if (_csn.valid() && _csn->getEllipsoidModel())
    {
        _csn->getEllipsoidModel()->convertXYZToLatLongHeight(x, y, z, lat, lon, height);
        lat = osg::RadiansToDegrees(lat);
        lon = osg::RadiansToDegrees(lon);
    }
    else
    {
        lon = x;
        lat = y;
        height = z;
    }*/
}

osgEarth::Util::Viewpoint
MapControl::getViewpoint()
{
    osgGA::KeySwitchMatrixManipulator* ksm = dynamic_cast<osgGA::KeySwitchMatrixManipulator*>(_viewer->getCameraManipulator());
    if (ksm)
    {
        osgEarth::Util::EarthManipulator* em = dynamic_cast<osgEarth::Util::EarthManipulator*>(ksm->getCurrentMatrixManipulator());
        if (em)
        {
            return em->getViewpoint();
        }
        else
        {
            OE_DEBUG << "Warning: cannot get viewpoint for manipulators other than EarthManipulator." << std::endl;
        }
    }
	  
	  return osgEarth::Util::Viewpoint();
}

void
MapControl::addCommandFactory(CommandFactory* factory)
{
    _commandFactories.push_back(factory);
}

std::string
MapControl::handleCommand(const std::string &command, const std::string &args, bool blocking)
{
    static std::string EMPTY_STRING;

    CommandArguments arguments(args);

    osg::notify(osg::INFO) << "MapControl::handleCommand " << command << "(" << args << ")" << std::endl;

    bool handled = false;
    for (CommandFactoryList::iterator itr = _commandFactories.begin(); itr != _commandFactories.end(); ++itr)
    {
        osg::ref_ptr<Command> cmd = itr->get()->create(command, arguments);
        if (cmd.valid())
        {
            osg::ref_ptr<osg::RefBlock> block;

            if (blocking)
            {
                block = new osg::RefBlock;
                cmd->setBlock(block);
            }

            _commandQueue->add(cmd.get());

            if (blocking)
            {
                block->block();
                return cmd->getResult();
            }
            handled = true;
            break;
        }
    }
    if (!handled)
    {
        osg::notify(osg::NOTICE) << "Could not create command for " << command << "(" << args << ")" << std::endl;
    }
    return EMPTY_STRING;
}

void
MapControl::postEvent(const std::string &target, const std::string &eventName, const std::string &data)
{
    EventData* ed = new EventData(target, eventName, data);
    PostMessage(_hwnd, WM_GODZI_EVENT, (WPARAM)ed, 0L);
}
