from dearpygui import dearpygui as dpg


def create_gruvbox_dark_theme():
    """
    Clean Gruvbox Dark theme with muted blues, greens, and yellows.
    Properly uses category parameters for consistent theming.
    """

    # add a font registry
    with dpg.font_registry():
        # first argument ids the path to the .ttf or .otf file
        font = dpg.add_font("./Fonts/Quantico-Regular.ttf", 20)
        dpg.bind_font(font)

    # Core Gruvbox palette
    BG_DARK = (29, 32, 33, 255)
    BG_DARKER = (23, 25, 26, 255)
    BG_LIGHT = (40, 40, 40, 255)

    FRAME_BG = (60, 56, 54, 255)
    FRAME_BG_HOVER = (80, 73, 69, 255)
    FRAME_BG_ACTIVE = (102, 92, 84, 255)

    # Text colors - Gruvbox cream/beige
    TEXT_PRIMARY = (235, 219, 178, 255)
    TEXT_DISABLED = (146, 131, 116, 255)
    TEXT_SELECTED_BG = (69, 133, 136, 200)

    # Accent colors - Blues and Teals
    ACCENT_BLUE = (43, 67, 68, 255)
    ACCENT_BLUE_LIGHT = (62, 96, 98, 255)
    ACCENT_TEAL = (131, 165, 152, 255)

    # Accent colors - Greens
    ACCENT_GREEN = (142, 192, 124, 255)
    ACCENT_YELLOW_GREEN = (184, 187, 38, 255)

    # UI element colors
    BORDER = (50, 48, 47, 255)
    BORDER_SHADOW = (0, 0, 0, 0)

    SEPARATOR = (102, 92, 84, 127)
    SEPARATOR_HOVER = (131, 165, 152, 200)
    SEPARATOR_ACTIVE = (142, 192, 124, 255)

    # Scrollbar
    SCROLLBAR_BG = (0, 0, 0, 40)
    SCROLLBAR_GRAB = (60, 56, 54, 255)
    SCROLLBAR_GRAB_HOVER = (80, 73, 69, 255)
    SCROLLBAR_GRAB_ACTIVE = (102, 92, 84, 255)

    with dpg.theme() as theme:
        with dpg.theme_component(dpg.mvAll):
            # ═══════════════════════════════════════════════════════════
            # CORE COLORS (category=dpg.mvThemeCat_Core)
            # ═══════════════════════════════════════════════════════════

            # Background colors
            dpg.add_theme_color(
                dpg.mvThemeCol_WindowBg, BG_DARK, category=dpg.mvThemeCat_Core
            )
            dpg.add_theme_color(
                dpg.mvThemeCol_ChildBg, BG_DARKER, category=dpg.mvThemeCat_Core
            )
            dpg.add_theme_color(
                dpg.mvThemeCol_PopupBg, BG_LIGHT, category=dpg.mvThemeCat_Core
            )
            dpg.add_theme_color(
                dpg.mvThemeCol_MenuBarBg, BG_LIGHT, category=dpg.mvThemeCat_Core
            )

            # Border colors
            dpg.add_theme_color(
                dpg.mvThemeCol_Border, BORDER, category=dpg.mvThemeCat_Core
            )
            dpg.add_theme_color(
                dpg.mvThemeCol_BorderShadow, BORDER_SHADOW, category=dpg.mvThemeCat_Core
            )

            # Text colors
            dpg.add_theme_color(
                dpg.mvThemeCol_Text, TEXT_PRIMARY, category=dpg.mvThemeCat_Core
            )
            dpg.add_theme_color(
                dpg.mvThemeCol_TextDisabled, TEXT_DISABLED, category=dpg.mvThemeCat_Core
            )
            dpg.add_theme_color(
                dpg.mvThemeCol_TextSelectedBg,
                TEXT_SELECTED_BG,
                category=dpg.mvThemeCat_Core,
            )

            # Frame colors (inputs, sliders, etc)
            dpg.add_theme_color(
                dpg.mvThemeCol_FrameBg, FRAME_BG, category=dpg.mvThemeCat_Core
            )
            dpg.add_theme_color(
                dpg.mvThemeCol_FrameBgHovered,
                FRAME_BG_HOVER,
                category=dpg.mvThemeCat_Core,
            )
            dpg.add_theme_color(
                dpg.mvThemeCol_FrameBgActive,
                FRAME_BG_ACTIVE,
                category=dpg.mvThemeCat_Core,
            )

            # Button colors
            dpg.add_theme_color(
                dpg.mvThemeCol_Button, ACCENT_BLUE, category=dpg.mvThemeCat_Core
            )
            dpg.add_theme_color(
                dpg.mvThemeCol_ButtonHovered,
                ACCENT_BLUE_LIGHT,
                category=dpg.mvThemeCat_Core,
            )
            dpg.add_theme_color(
                dpg.mvThemeCol_ButtonActive,
                ACCENT_BLUE_LIGHT,
                category=dpg.mvThemeCat_Core,
            )

            # Header colors (collapsing headers, tree nodes)
            dpg.add_theme_color(
                dpg.mvThemeCol_Header, ACCENT_BLUE, category=dpg.mvThemeCat_Core
            )
            dpg.add_theme_color(
                dpg.mvThemeCol_HeaderHovered,
                ACCENT_BLUE_LIGHT,
                category=dpg.mvThemeCat_Core,
            )
            dpg.add_theme_color(
                dpg.mvThemeCol_HeaderActive,
                ACCENT_BLUE_LIGHT,
                category=dpg.mvThemeCat_Core,
            )

            # Title bar colors
            dpg.add_theme_color(
                dpg.mvThemeCol_TitleBg, BG_LIGHT, category=dpg.mvThemeCat_Core
            )
            dpg.add_theme_color(
                dpg.mvThemeCol_TitleBgActive, ACCENT_BLUE, category=dpg.mvThemeCat_Core
            )
            dpg.add_theme_color(
                dpg.mvThemeCol_TitleBgCollapsed, BG_DARK, category=dpg.mvThemeCat_Core
            )

            # Tab colors
            dpg.add_theme_color(
                dpg.mvThemeCol_Tab, ACCENT_BLUE, category=dpg.mvThemeCat_Core
            )
            dpg.add_theme_color(
                dpg.mvThemeCol_TabHovered,
                ACCENT_BLUE_LIGHT,
                category=dpg.mvThemeCat_Core,
            )
            dpg.add_theme_color(
                dpg.mvThemeCol_TabActive,
                ACCENT_BLUE_LIGHT,
                category=dpg.mvThemeCat_Core,
            )
            dpg.add_theme_color(
                dpg.mvThemeCol_TabUnfocused, BG_DARK, category=dpg.mvThemeCat_Core
            )
            dpg.add_theme_color(
                dpg.mvThemeCol_TabUnfocusedActive,
                BG_LIGHT,
                category=dpg.mvThemeCat_Core,
            )

            # Scrollbar colors
            dpg.add_theme_color(
                dpg.mvThemeCol_ScrollbarBg, SCROLLBAR_BG, category=dpg.mvThemeCat_Core
            )
            dpg.add_theme_color(
                dpg.mvThemeCol_ScrollbarGrab,
                SCROLLBAR_GRAB,
                category=dpg.mvThemeCat_Core,
            )
            dpg.add_theme_color(
                dpg.mvThemeCol_ScrollbarGrabHovered,
                SCROLLBAR_GRAB_HOVER,
                category=dpg.mvThemeCat_Core,
            )
            dpg.add_theme_color(
                dpg.mvThemeCol_ScrollbarGrabActive,
                SCROLLBAR_GRAB_ACTIVE,
                category=dpg.mvThemeCat_Core,
            )

            # Slider and check colors
            dpg.add_theme_color(
                dpg.mvThemeCol_SliderGrab, ACCENT_GREEN, category=dpg.mvThemeCat_Core
            )
            dpg.add_theme_color(
                dpg.mvThemeCol_SliderGrabActive,
                ACCENT_YELLOW_GREEN,
                category=dpg.mvThemeCat_Core,
            )
            dpg.add_theme_color(
                dpg.mvThemeCol_CheckMark,
                ACCENT_YELLOW_GREEN,
                category=dpg.mvThemeCat_Core,
            )

            # Separator colors
            dpg.add_theme_color(
                dpg.mvThemeCol_Separator, SEPARATOR, category=dpg.mvThemeCat_Core
            )
            dpg.add_theme_color(
                dpg.mvThemeCol_SeparatorHovered,
                SEPARATOR_HOVER,
                category=dpg.mvThemeCat_Core,
            )
            dpg.add_theme_color(
                dpg.mvThemeCol_SeparatorActive,
                SEPARATOR_ACTIVE,
                category=dpg.mvThemeCat_Core,
            )

            # Resize grip colors
            dpg.add_theme_color(
                dpg.mvThemeCol_ResizeGrip, ACCENT_TEAL, category=dpg.mvThemeCat_Core
            )
            dpg.add_theme_color(
                dpg.mvThemeCol_ResizeGripHovered,
                ACCENT_GREEN,
                category=dpg.mvThemeCat_Core,
            )
            dpg.add_theme_color(
                dpg.mvThemeCol_ResizeGripActive,
                ACCENT_GREEN,
                category=dpg.mvThemeCat_Core,
            )

            # Drag and drop
            dpg.add_theme_color(
                dpg.mvThemeCol_DragDropTarget,
                ACCENT_YELLOW_GREEN,
                category=dpg.mvThemeCat_Core,
            )

            # Table colors
            dpg.add_theme_color(
                dpg.mvThemeCol_TableHeaderBg, BG_LIGHT, category=dpg.mvThemeCat_Core
            )
            dpg.add_theme_color(
                dpg.mvThemeCol_TableBorderStrong, BORDER, category=dpg.mvThemeCat_Core
            )
            dpg.add_theme_color(
                dpg.mvThemeCol_TableBorderLight,
                (56, 60, 54, 255),
                category=dpg.mvThemeCat_Core,
            )
            dpg.add_theme_color(
                dpg.mvThemeCol_TableRowBg, (0, 0, 0, 0), category=dpg.mvThemeCat_Core
            )
            dpg.add_theme_color(
                dpg.mvThemeCol_TableRowBgAlt,
                (255, 255, 255, 10),
                category=dpg.mvThemeCat_Core,
            )

            # ═══════════════════════════════════════════════════════════
            # PLOT COLORS (category=dpg.mvThemeCat_Plots)
            # ═══════════════════════════════════════════════════════════

            dpg.add_theme_color(
                dpg.mvPlotCol_FrameBg, FRAME_BG, category=dpg.mvThemeCat_Plots
            )
            dpg.add_theme_color(
                dpg.mvPlotCol_PlotBg, BG_DARKER, category=dpg.mvThemeCat_Plots
            )
            dpg.add_theme_color(
                dpg.mvPlotCol_Line, ACCENT_TEAL, category=dpg.mvThemeCat_Plots
            )
            dpg.add_theme_color(
                dpg.mvPlotCol_Fill, ACCENT_GREEN, category=dpg.mvThemeCat_Plots
            )
            dpg.add_theme_color(
                dpg.mvPlotCol_LegendBg, BG_LIGHT, category=dpg.mvThemeCat_Plots
            )
            dpg.add_theme_color(
                dpg.mvPlotCol_LegendBorder, BORDER, category=dpg.mvThemeCat_Plots
            )
            dpg.add_theme_color(
                dpg.mvPlotCol_PlotBorder, BORDER, category=dpg.mvThemeCat_Plots
            )

            # ═══════════════════════════════════════════════════════════
            # NODE EDITOR COLORS (category=dpg.mvThemeCat_Nodes)
            # ═══════════════════════════════════════════════════════════

            # Node background colors
            dpg.add_theme_color(
                dpg.mvNodeCol_NodeBackground,
                (30, 28, 27, 170),
                category=dpg.mvThemeCat_Nodes,
            )
            dpg.add_theme_color(
                dpg.mvNodeCol_NodeBackgroundHovered,
                FRAME_BG,
                category=dpg.mvThemeCat_Nodes,
            )
            dpg.add_theme_color(
                dpg.mvNodeCol_NodeBackgroundSelected,
                (30, 28, 27, 255),
                category=dpg.mvThemeCat_Nodes,
            )
            dpg.add_theme_color(
                dpg.mvNodeCol_NodeOutline, ACCENT_TEAL, category=dpg.mvThemeCat_Nodes
            )

            # Node title bar colors
            dpg.add_theme_color(
                dpg.mvNodeCol_TitleBar, ACCENT_BLUE, category=dpg.mvThemeCat_Nodes
            )
            dpg.add_theme_color(
                dpg.mvNodeCol_TitleBarHovered,
                ACCENT_BLUE_LIGHT,
                category=dpg.mvThemeCat_Nodes,
            )
            dpg.add_theme_color(
                dpg.mvNodeCol_TitleBarSelected,
                ACCENT_BLUE_LIGHT,
                category=dpg.mvThemeCat_Nodes,
            )

            # Link colors
            dpg.add_theme_color(
                dpg.mvNodeCol_Link, ACCENT_YELLOW_GREEN, category=dpg.mvThemeCat_Nodes
            )
            dpg.add_theme_color(
                dpg.mvNodeCol_LinkHovered, ACCENT_GREEN, category=dpg.mvThemeCat_Nodes
            )
            dpg.add_theme_color(
                dpg.mvNodeCol_LinkSelected,
                ACCENT_TEAL,
                category=dpg.mvThemeCat_Nodes,
            )

            # Pin colors
            dpg.add_theme_color(
                dpg.mvNodeCol_Pin, ACCENT_GREEN, category=dpg.mvThemeCat_Nodes
            )
            dpg.add_theme_color(
                dpg.mvNodeCol_PinHovered, ACCENT_TEAL, category=dpg.mvThemeCat_Nodes
            )

            # Selection box
            dpg.add_theme_color(
                dpg.mvNodeCol_BoxSelector,
                (142, 192, 124, 64),
                category=dpg.mvThemeCat_Nodes,
            )
            dpg.add_theme_color(
                dpg.mvNodeCol_BoxSelectorOutline,
                ACCENT_GREEN,
                category=dpg.mvThemeCat_Nodes,
            )

            # Grid colors
            dpg.add_theme_color(
                dpg.mvNodeCol_GridBackground, BG_DARKER, category=dpg.mvThemeCat_Nodes
            )
            dpg.add_theme_color(
                dpg.mvNodeCol_GridLine, (60, 56, 54, 255), category=dpg.mvThemeCat_Nodes
            )
            dpg.add_theme_color(
                dpg.mvNodesCol_GridLinePrimary,
                (80, 73, 69, 255),
                category=dpg.mvThemeCat_Nodes,
            )

            # Minimap colors
            dpg.add_theme_color(
                dpg.mvNodesCol_MiniMapBackground,
                (*BG_DARK[:3], 150),
                category=dpg.mvThemeCat_Nodes,
            )
            dpg.add_theme_color(
                dpg.mvNodesCol_MiniMapBackgroundHovered,
                (*BG_LIGHT[:3], 220),
                category=dpg.mvThemeCat_Nodes,
            )
            dpg.add_theme_color(
                dpg.mvNodesCol_MiniMapOutline, BORDER, category=dpg.mvThemeCat_Nodes
            )
            dpg.add_theme_color(
                dpg.mvNodesCol_MiniMapOutlineHovered,
                ACCENT_TEAL,
                category=dpg.mvThemeCat_Nodes,
            )
            dpg.add_theme_color(
                dpg.mvNodesCol_MiniMapNodeBackground,
                (30, 28, 27, 255),
                category=dpg.mvThemeCat_Nodes,
            )
            dpg.add_theme_color(
                dpg.mvNodesCol_MiniMapNodeBackgroundHovered,
                FRAME_BG,
                category=dpg.mvThemeCat_Nodes,
            )
            dpg.add_theme_color(
                dpg.mvNodesCol_MiniMapNodeBackgroundSelected,
                ACCENT_BLUE,
                category=dpg.mvThemeCat_Nodes,
            )
            dpg.add_theme_color(
                dpg.mvNodesCol_MiniMapNodeOutline, BORDER, category=dpg.mvThemeCat_Nodes
            )
            dpg.add_theme_color(
                dpg.mvNodesCol_MiniMapLink, ACCENT_TEAL, category=dpg.mvThemeCat_Nodes
            )
            dpg.add_theme_color(
                dpg.mvNodesCol_MiniMapLinkSelected,
                ACCENT_YELLOW_GREEN,
                category=dpg.mvThemeCat_Nodes,
            )
            dpg.add_theme_color(
                dpg.mvNodesCol_MiniMapCanvas,
                (*BG_DARKER[:3], 128),
                category=dpg.mvThemeCat_Nodes,
            )
            dpg.add_theme_color(
                dpg.mvNodesCol_MiniMapCanvasOutline,
                ACCENT_YELLOW_GREEN,
                category=dpg.mvThemeCat_Nodes,
            )

            # ═══════════════════════════════════════════════════════════
            # STYLE VARIABLES
            # ═══════════════════════════════════════════════════════════

            # Rounding (flat design)
            dpg.add_theme_style(
                dpg.mvStyleVar_FrameRounding, 0, category=dpg.mvThemeCat_Core
            )
            dpg.add_theme_style(
                dpg.mvStyleVar_WindowRounding, 0, category=dpg.mvThemeCat_Core
            )
            dpg.add_theme_style(
                dpg.mvStyleVar_ChildRounding, 0, category=dpg.mvThemeCat_Core
            )
            dpg.add_theme_style(
                dpg.mvStyleVar_PopupRounding, 0, category=dpg.mvThemeCat_Core
            )
            dpg.add_theme_style(
                dpg.mvStyleVar_ScrollbarRounding, 0, category=dpg.mvThemeCat_Core
            )
            dpg.add_theme_style(
                dpg.mvStyleVar_GrabRounding, 0, category=dpg.mvThemeCat_Core
            )
            dpg.add_theme_style(
                dpg.mvStyleVar_TabRounding, 0, category=dpg.mvThemeCat_Core
            )

            # Padding
            dpg.add_theme_style(
                dpg.mvStyleVar_FramePadding, 4, 4, category=dpg.mvThemeCat_Core
            )

            # Borders
            dpg.add_theme_style(
                dpg.mvStyleVar_FrameBorderSize, 1, category=dpg.mvThemeCat_Core
            )
            dpg.add_theme_style(
                dpg.mvStyleVar_WindowBorderSize, 1, category=dpg.mvThemeCat_Core
            )
            dpg.add_theme_style(
                dpg.mvStyleVar_ChildBorderSize, 1, category=dpg.mvThemeCat_Core
            )
            dpg.add_theme_style(
                dpg.mvStyleVar_PopupBorderSize, 1, category=dpg.mvThemeCat_Core
            )

    dpg.bind_theme(theme)
    return theme
