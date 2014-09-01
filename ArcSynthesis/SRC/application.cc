#include <precompiled.h>
#include <application.h>

LRESULT CALLBACK WndProc( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam )
{
    if (msg == WM_NCCREATE) 
    {
        LPCREATESTRUCT createStruct = reinterpret_cast<LPCREATESTRUCT>(lParam);
        SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(createStruct->lpCreateParams));
	}

    Application *app = NULL;
    app = (Application *)GetWindowLongPtr( hWnd, GWLP_USERDATA );
    if( app != NULL )
    {
        return app->WindowProc( hWnd, msg, wParam, lParam );
    }

    return DefWindowProc( hWnd, msg, wParam, lParam );
}


Application::Application( const std::wstring &set_name )
{
    if( set_name.size() <= 0 )
    {
        applicationName = L"GL_application";
    }
    else
    {
        applicationName = set_name;
    }

    className = set_name + L"_class";

    maxFrameTime = 0.0625;

    lastTime = 0.0;
    accumulatedTime = 0.0;
    timeStep = 0.008;

    initialized = false;
    done = false;
    changedResolution = false;

    windowActivated = false;
    windowInitialized = false;

    color = glm::vec3( 0.2, 0.4, 0.6 );
    speed = glm::vec3( 0.1, 0.2, -0.4 );

    keyboard = nullptr;
    renderer = nullptr;
    timer = nullptr;
    config = nullptr;
}

Application::~Application( void )
{
    // gotta love smart pointers
}

bool Application::Initialize( void )
{
    config = std::make_shared<Configuration>();
    config->LoadConfiguration( "config.xml" );

    renderer = std::make_shared<GLRenderer>();
    if( initializeWindow() == false )
    {
        MessageBox( NULL, L"Failed to initialize window.", L"SYSTEM ERROR", MB_OK | MB_ICONERROR );
        initialized = false;
        windowActivated = false;

        return false;
    }
    
    keyboard = std::make_shared<Keyboard>();

    timer = std::make_shared<Timer>();
    timer->SetTime();

    initialized = true;

    return true;
}

bool Application::Run( void )
{
    MSG message;
    ZeroMemory( &message, sizeof( message ) );

    while( done == false )
    {
        if( PeekMessage( &message, NULL, 0, 0, PM_REMOVE ) )
        {
            TranslateMessage( &message );
            DispatchMessage( &message );
        }

        if( ( WM_QUIT == message.message ) || ( false == initialized ) )
        {
            done = true;
            break;
        }

        // ESC is the default quit. Probably will change this later once
        // I need a more robust state system.
        if( keyboard->IsKeydown( VK_ESCAPE ) )
        {
            done = true;
            break;
        }

        if( keyboard->IsKeyPressed( VK_NEXT ) )
        {
            toggleFullscreen();
            continue;
        }

        // Fixed time-step, full speed render. To accomplish this, we
        // use an accumulator.

        // First, we get the time since the last render.
        double frame_time = timer->GetDeltaTime();
        if( maxFrameTime < frame_time )
        {
            frame_time = maxFrameTime;
        }

        // Then we add it to the accumulator.
        accumulatedTime += frame_time;

        // We then subtract off fixed chunks of time and simulate according
        // to that time step. This gives us a fixed simulation time-step, while
        // allowing us to render with a variable time-step.
        while( accumulatedTime > timeStep )
        {
            accumulatedTime -= timeStep;
            step();
        }

        draw();
    }

    return true;
}


void Application::Shutdown( void )
{
    renderer->Shutdown();
    shutdownWindow();
}

void Application::draw( void )
{
    renderer->SetClearColor( color );
    renderer->Draw();
}

bool Application::initializeWindow( void )
{
    hInstance = GetModuleHandle( NULL );

    // Standard windows boilerplate
    WNDCLASSEX wcx;
    ZeroMemory( &wcx, sizeof( wcx ) );

    wcx.cbClsExtra = 0;
    wcx.cbWndExtra = 0;
    wcx.hbrBackground = ( HBRUSH )GetStockObject( GRAY_BRUSH );
    wcx.hCursor = LoadCursor( NULL, IDC_CROSS );
    wcx.hIcon = LoadIcon( NULL, IDI_APPLICATION );
    wcx.hIconSm = wcx.hIcon;
    wcx.hInstance = hInstance;
    wcx.lpfnWndProc = WndProc;
    wcx.lpszClassName = className.c_str();
    wcx.lpszMenuName = NULL;
    wcx.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wcx.cbSize = sizeof( wcx );

    RegisterClassEx( &wcx );

    // The following code assumes we only have one monitor and are displaying on the primary monitor
    glm::ivec2 screenDimensions;
    int value = GetSystemMetrics( SM_CXSCREEN );
    screenDimensions.x = value;
    
    value = GetSystemMetrics( SM_CYSCREEN );
    screenDimensions.y = value;

    config->GraphicsConfig()->SetScreenDimensions( screenDimensions );

    if( config->GraphicsConfig()->GetFullscreen() == false )
    {
        // The following code sets up a windowed display that is centered on the screen
        if( config->GraphicsConfig()->GetWindowWidth() > (screenDimensions.x - 20) )
        {
            config->GraphicsConfig()->SetWindowWidth( screenDimensions.x - 20 );
        }
        if( config->GraphicsConfig()->GetWindowHeight() > (screenDimensions.y - 20) )
        {
            config->GraphicsConfig()->SetWindowHeight( screenDimensions.y - 20 );
        }

    	int x_position = ( screenDimensions.x - config->GraphicsConfig()->GetWindowWidth() ) / 2;
    	int y_position = ( screenDimensions.y - config->GraphicsConfig()->GetWindowHeight() ) / 2;

    	config->GraphicsConfig()->SetWindowPosition( glm::ivec2( x_position, y_position ) );
    }
    else
    {
        // Otherwise we do a fullscreen window.
        config->GraphicsConfig()->SetWindowDimensions( screenDimensions );
        config->GraphicsConfig()->SetWindowPosition( glm::ivec2( 0, 0 ) );
    }

    // Here we create a temporary window so that we can initialize the OpenGL extension methods used to create
    // the context we want.
    HWND temp_h_wnd = CreateWindowEx( NULL, className.c_str(), applicationName.c_str(), WS_POPUP, 100, 100, 100, 100,
                                      NULL, NULL, hInstance, this );
    if( NULL == temp_h_wnd )
    {
        return false;
    }
    ShowWindow( temp_h_wnd, SW_HIDE );
    if( renderer->initializeTemporaryExtensions( temp_h_wnd ) == false )
    {
        DestroyWindow( temp_h_wnd );
        temp_h_wnd = NULL;

        return false;
    }

    // And now we can finally create the target context
    hWnd = CreateWindowEx( NULL, 
                           className.c_str(), 
                           applicationName.c_str(), 
                           WS_POPUP, 
                           config->GraphicsConfig()->GetWindowXPosition(), 
                           config->GraphicsConfig()->GetWindowYPosition(),
                           config->GraphicsConfig()->GetWindowWidth(),
                           config->GraphicsConfig()->GetWindowHeight(),
                           NULL, 
                           NULL, 
                           hInstance, 
                           this );
    if( NULL == hWnd )
    {
        return false;
    }
    windowActivated = false;
    if( renderer->Initialize( hWnd ) == false )
    {
        DestroyWindow( hWnd );
        hWnd = NULL;

        return false;
    }

    // Destroy the temporary window
    DestroyWindow( temp_h_wnd );
    temp_h_wnd = NULL;


    // And we're done!
    ShowWindow( hWnd, SW_SHOW );
    SetFocus( hWnd );
    SetForegroundWindow( hWnd );

    windowActivated = true;
    windowInitialized = true;

    return true;
}

bool Application::resizeWindow( unsigned int width, unsigned int height )
{
    // First set the dimensions. This also handles sizing the dimensions( window will never be smaller than 100x100 )
    config->GraphicsConfig()->SetWindowDimensions( glm::ivec2( width, height ) );
    int windowWidth = config->GraphicsConfig()->GetWindowWidth();
    int windowHeight = config->GraphicsConfig()->GetWindowHeight();

    // note, these will work fine for fullscreen as well, because screen width - fullscreen window size = 0
    int x = ( config->GraphicsConfig()->GetScreenWidth() - windowWidth ) / 2;
    int y = ( config->GraphicsConfig()->GetScreenHeight() - windowHeight ) / 2;

    config->GraphicsConfig()->SetWindowPosition( glm::ivec2( x, y ) );
    
    ShowWindow( hWnd, SW_HIDE );

    SetWindowPos( hWnd, HWND_TOP,
                  x, y,
                  windowWidth,
                  windowHeight,
                  SWP_SHOWWINDOW );
    
    ShowWindow( hWnd, SW_SHOW );

    return true;
}

bool Application::resizeWindow( unsigned int x, unsigned int y, unsigned int width, unsigned int height )
{
    // We call set window dim/pos which handles the constraints, then retrieve the constrained values.
    config->GraphicsConfig()->SetWindowDimensions( glm::ivec2( width, height ) );
    int windowWidth = config->GraphicsConfig()->GetWindowWidth();
    int windowHeight = config->GraphicsConfig()->GetWindowHeight();

    config->GraphicsConfig()->SetWindowPosition( glm::ivec2( x, y ) );
    int xPos = config->GraphicsConfig()->GetWindowXPosition();
    int yPos = config->GraphicsConfig()->GetWindowYPosition();


    SetWindowPos( hWnd, HWND_TOP,
                  xPos, yPos,
                  windowWidth,
                  windowHeight,
                  SWP_SHOWWINDOW );
    return true;
}

void Application::shutdownWindow( void )
{
    renderer->Shutdown();

    // If we maximized the window, reset the display back to default
    if( changedResolution == true )
    {
        ChangeDisplaySettings( NULL, 0 );
    }

    // Cleans up the window properly
    DestroyWindow( hWnd );
    UnregisterClass( className.c_str(), hInstance );
    hInstance = NULL;
}

void Application::toggleFullscreen( void )
{
    bool wasFullscreen = config->GraphicsConfig()->GetFullscreen();
    config->GraphicsConfig()->SetFullscreen( !wasFullscreen );
    if( wasFullscreen == false )
    {
        // was fullscreen -> windowed
        resizeWindow( config->GraphicsConfig()->GetScreenWidth(),
                      config->GraphicsConfig()->GetScreenHeight() );
    }
    else
    {
        // was windowed -> fullscreen
        resizeWindow( config->GraphicsConfig()->GetDefaultWindowWidth(),
                      config->GraphicsConfig()->GetDefaultWindowHeight() );
    }
}

void Application::step( void )
{
    color.r += speed.r * timeStep;
    color.g += speed.g * timeStep;
    color.b += speed.b * timeStep;

    if( color.r >= 1.0 )
    {
        color.r = 1.0;
        speed.r = -speed.r;
    }
    else if( color.r <= 0.0 )
    {
        color.r = 0.0;
        speed.r = -speed.r;
    }
    if( color.g >= 1.0 )
    {
        color.g = 1.0;
        speed.g = -speed.g;
    }
    else if( color.g <= 0.0 )
    {
        color.g = 0.0;
        speed.g = -speed.g;
    }
    if( color.b >= 1.0 )
    {
        color.b = 1.0;
        speed.b = -speed.b;
    }
    else if( color.b <= 0.0 )
    {
        color.b = 0.0;
        speed.b = -speed.b;
    }
}

bool Application::IsInitialized( void ) const
{
    return initialized;
}

LRESULT CALLBACK Application::WindowProc( HWND handle, UINT message, WPARAM w_param, LPARAM l_param )
{
    unsigned int key_code = 0;
    switch( message )
    {
    case WM_CLOSE:
        PostQuitMessage( 0 );
        return 0;
    case WM_KEYDOWN:
        key_code = static_cast< unsigned int >( w_param );
        if( ( key_code >= 0 ) && ( key_code < 256 ) )
        {
            // On the first keydown
            if( ( l_param & ( 1 << 30 ) ) == 0 )
            {
                keyboard->setKeyPressed( key_code, true);
            }
            
            if( keyboard->IsKeydown(key_code) )
            {
                keyboard->setKeyPressed(key_code, false);
            }

            keyboard->setKeyDown( key_code );
        }
        break;
    case WM_KEYUP:
        key_code = static_cast< unsigned int >( w_param );
        if( ( key_code >= 0 ) && ( key_code < 256 ) )
        {
            keyboard->setKeyUp( key_code );
        }
        break;
    case WM_ACTIVATE:
        if( true == windowInitialized )
        {
            // These lines handle the always-on-top behavior. In other words, if the window is active,
            // then it's at the top of the window Z-order, otherwise it's at the bottom.
            if( true == windowActivated )
            {
                windowActivated = false;
                SetWindowPos( hWnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE );
            }
            else
            {
                windowActivated = true;
                SetWindowPos( hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE );
            }
        }
        break;
    default:
        break;
    }
    return DefWindowProc( handle, message, w_param, l_param );
}