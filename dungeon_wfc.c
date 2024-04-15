#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

#include "raylib.h"

#define BOARD_WIDTH (18)
#define BOARD_SIZE (BOARD_WIDTH * BOARD_WIDTH)

#define TILE_SIZE (128)
#define TILE_CENTER (TILE_SIZE / 2)
#define BOX_SIZE (TILE_SIZE / 3)
#define BOARD_PADDING (16)

#define BOARD_TEXTURE_WIDTH (BOARD_WIDTH * TILE_SIZE + BOARD_PADDING * 2)

#define SCREEN_WIDTH (800)
#define SCREEN_HEIGHT (800)

#define TILE_STATES (30)

static float screen_scale = SCREEN_WIDTH / (float) BOARD_TEXTURE_WIDTH;

Texture2D *tilesheet = NULL;
static const int tile_sheet_tile_size = 16;
int tile_scale = TILE_SIZE / tile_sheet_tile_size;

// The tiles are each stored as an integer,
// with the first 9 bits representing its superpositions.
// If a bit is set, the tile is allowed to be that number.
static int tiles[BOARD_SIZE] = { 0 };

// Load a single tile from a tilesheet.
Texture2D load_tile_from_tilesheet(const char *file, int tile_size, int row, int column) {
    fprintf(stderr, "Loading tile from tilesheet: %s\n", file);
    Image image = LoadImage(file);

    Rectangle source = { column * tile_size, row * tile_size, tile_size, tile_size };
    Image tile = ImageFromImage(image, source);

    Texture2D texture = LoadTextureFromImage(tile);
    UnloadImage(tile);

    return texture;
}

void free_tilesheet(Texture2D *tilesheet, int tile_count) {
    for (int i = 0; i < tile_count; ++i) UnloadTexture(tilesheet[i]);
    free(tilesheet);
}

// Reset all tiles to be a superposition of 1-9.
void reset_tiles(void) {
    // 0x1FF sets the first 9 bits to 1.
    // This indicates that the tile can be any number.
    for (int i = 0; i < BOARD_SIZE; ++i) tiles[i] = 0xFFFF;
}

// Get a pointer to a tile at a given position.
int *get_tile(int x, int y) {
    // The tiles are stored in a 1D array.
    // Multiplying by BOARD_WIDTH gets the row,
    // and adding the column gets the 1D index of the tile.
    return &tiles[y * BOARD_WIDTH + x];
}

int entropy(int x, int y) {
    int value = *get_tile(x, y);
    int count = 0;

    // Count the number of bits set in the tile's superpositions.
    for (int i = 0; i < TILE_STATES; ++i) {
        if (value & (1 << i)) ++count;
    }

    return count;
}

// Check if a tile's superpositions only contain one value.
bool is_collapsed(int x, int y) {
    return entropy(x, y) == 1;
}

bool is_set(int x, int y, int bit) {
    return *get_tile(x, y) & (1 << bit);
}

// Get the value of a collapsed tile.
int get_collapsed_value(int x, int y) {
    // Using log2 gets the index of the set bit
    // because a binary number with only one bit set
    // is some power of 2.
    int value = *get_tile(x, y);
    int mask = (1 << TILE_STATES) - 1;
    return (int) log2(value & mask);
}

// Forwards declaration for recursion.
void constrain_peers(int x, int y, int value);

// Remove a superposition from a tile
// by unsetting the bit at the index of the value.
void constrain_tile(int x, int y, int value) {
    *get_tile(x, y) &= ~(1 << value);

    // If the tile was just collapsed because of this constraint,
    // propagate the constraint to its peers.
    if (is_collapsed(x, y)) constrain_peers(x, y, get_collapsed_value(x, y));
}

// Constrain the superpositions of a tile's peers by removing a possible value.
void constrain_peers(int x, int y, int value) {
    // Constrain the row and column that the tile belongs to.
    for (int i = 0; i < BOARD_WIDTH; ++i) {
        // Skip the tile that set the constraint.
        if (i == x || i == y) continue;

        // Skip tiles that are already collapsed.
        if (!is_collapsed(i, y)) constrain_tile(i, y, value);
        if (!is_collapsed(x, i)) constrain_tile(x, i, value);
    }

    // Successively dividing and multiplying by 3,
    // effectively rounds down to the nearest multiple of 3.
    // This gives us the index of the top-left tile in the box.
    int box_x = x / 3 * 3;
    int box_y = y / 3 * 3;

    // Constrain the 3x3 box that the tile belongs to.
    for (int i = box_y; i < box_y + 3; ++i) {
        for (int j = box_x; j < box_x + 3; ++j) {
            // Skip the tile that set the constraint.
            if (i == y && j == x) continue;

            // Skip tiles that are already collapsed.
            if (!is_collapsed(j, i)) constrain_tile(j, i, value);
        }
    }
}

// Collapse a tile to a single value.
void collapse_tile(int x, int y, int value) {
    // Set the tile to only contain the collapsed value.
    *get_tile(x, y) = 1 << value;

    // Constrain the superpositions of the tile's in the same
    // row, column, and 3x3 box to not contain the collapsed value.
    constrain_peers(x, y, value);
}

// Draw a tile at a given board position.
void draw_tile(int x, int y) {
    int tile_x = x * TILE_SIZE + BOARD_PADDING;
    int tile_y = y * TILE_SIZE + BOARD_PADDING;

    // If the tile is collapsed, draw the collapsed value at the center.
    if (is_collapsed(x, y)) {
        int texture_index = get_collapsed_value(x, y);
        // Draw the tile scaled to the appropriate size on the board texture.
        DrawTexturePro(tilesheet[texture_index], (Rectangle) { 0, 0, tile_sheet_tile_size, tile_sheet_tile_size },
            (Rectangle) { tile_x, tile_y, TILE_SIZE, TILE_SIZE }, (Vector2) { 0, 0 }, 0, WHITE);
        return;
    }

    // If the tile is not collapsed, draw the remaining superpositions.
    for (int bit = 0; bit < TILE_STATES; ++bit) {
        // Do not draw the superposition if it is not set.
        if (!is_set(x, y, bit)) continue;

        int subtile_x = tile_x + bit / 3 * BOX_SIZE;
        int subtile_y = tile_y + bit % 3 * BOX_SIZE;

        Rectangle subtile_rect = {
            subtile_x, subtile_y,
            BOX_SIZE, BOX_SIZE
        };

        // Get the mouse position, scaled according to the screen scale.
        Vector2 mouse_pos = GetMousePosition();
        mouse_pos.x /= screen_scale;
        mouse_pos.y /= screen_scale;

        // Highlight the subtile if the mouse is hovering over it.
        bool is_hovered = CheckCollisionPointRec(mouse_pos, subtile_rect);
        if (is_hovered) DrawRectangleRec(subtile_rect, LIGHTGRAY);

        // Draw the superposition at the center of the subtile.
        int texture_index = bit;
        DrawTexturePro(tilesheet[texture_index], (Rectangle) { 0, 0, tile_sheet_tile_size, tile_sheet_tile_size },
            (Rectangle) { subtile_x, subtile_y, BOX_SIZE, BOX_SIZE }, (Vector2) { 0, 0 }, 0, WHITE);

        // If the user clicks on a subtile,
        // collapse the tile to the value of the subtile.
        // NOTE: I would like if this were handled in the main loop instead,
        // but I already had the tile positions available here.
        if (is_hovered && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            collapse_tile(x, y, bit);
        }
    }
}

// Draw the entire board.
void draw_board(void) {
    // Draw all tiles on the board.
    for (int iter = 0; iter < BOARD_SIZE; ++iter) {
        draw_tile(iter % BOARD_WIDTH, iter / BOARD_WIDTH);
    }
}

void init_textures(void) {
    tilesheet = (Texture2D *) malloc(TILE_STATES * sizeof(Texture2D));

    if (tilesheet == NULL) {
        fprintf(stderr, "Failed to load textures\n");
        exit(1);
    }

    //const char *file = "assets/atlas_floor-16x16.png";
    //int columns = 7;
    const char *file = "assets/atlas_walls_high-16x32.png";
    int columns = 14;
    for (int i = 0; i < TILE_STATES; ++i) {
        tilesheet[i] = load_tile_from_tilesheet(file, tile_sheet_tile_size, i / columns, i % columns);
    }
}

void free_textures(void) {
    free_tilesheet(tilesheet, TILE_STATES);
}

void solve_board(void) {
    SetRandomSeed(time(NULL));

    int sorted_tiles[BOARD_SIZE];
    for (int i = 0; i < BOARD_SIZE; ++i) sorted_tiles[i] = i;

    // Sort the tiles by entropy.
    for (int i = 0; i < BOARD_SIZE; ++i) {
        for (int j = i + 1; j < BOARD_SIZE; ++j) {
            int a = entropy(sorted_tiles[i] % BOARD_WIDTH, sorted_tiles[i] / BOARD_WIDTH);
            int b = entropy(sorted_tiles[j] % BOARD_WIDTH, sorted_tiles[j] / BOARD_WIDTH);
            if (a > b) {
                int temp = sorted_tiles[i];
                sorted_tiles[i] = sorted_tiles[j];
                sorted_tiles[j] = temp;
            }
        }
    }

    int total_entropy = 0;
    for (int i = 0; i < BOARD_SIZE; ++i) {
        int x = sorted_tiles[i] % BOARD_WIDTH;
        int y = sorted_tiles[i] / BOARD_WIDTH;
        total_entropy += entropy(x, y);
    }

    if (total_entropy == 0) {
        fprintf(stderr, "Invalid board state\n");
        return;
    }
    if (total_entropy == BOARD_SIZE) {
        return;
    }

    // Collapse the first non-collapsed tile with the lowest entropy.
    for (int i = 0; i < BOARD_SIZE; ++i) {
        int x = sorted_tiles[i] % BOARD_WIDTH;
        int y = sorted_tiles[i] / BOARD_WIDTH;

        if (!is_collapsed(x, y)) {
            int value = *get_tile(x, y);
            int bit = GetRandomValue(0, 8);
            while (!(value & (1 << bit))) bit = (bit + 1) % TILE_STATES;
            collapse_tile(x, y, bit);
            break;
        }
    }

    // Recursively solve the board.
    solve_board();
}

int main(void) {
    int width = SCREEN_WIDTH;
    int height = SCREEN_HEIGHT;

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(width, height, "Sudoku WFC");
    SetTargetFPS(60);

    // Initialize the tiles to be a superposition of 1-9.
    reset_tiles();

    // Create a render texture to draw the board to.
    RenderTexture2D board_texture = LoadRenderTexture(BOARD_TEXTURE_WIDTH, BOARD_TEXTURE_WIDTH);

    // The source rectangle is the size of the board texture.
    // The height component is negative because OpenGL's coordinate system
    // uses the bottom-left corner as the origin instead of the top-left.
    Rectangle source = { 0, 0, BOARD_TEXTURE_WIDTH, -BOARD_TEXTURE_WIDTH };

    // The destination rectangle is the entire size of the window.
    Rectangle dest = { 0, 0, width, height };

    // Camera2D objects to handle zooming and panning.
    // In this case, they don't do anything special besides being the target of the render textures.
    Camera2D board_camera = { .zoom = 1.f };
    Camera2D screen_camera = { .zoom = 1.f };

    init_textures();
    solve_board();

    while (!WindowShouldClose()) {
        // Update the title to contain the time it took to show the last frame.
        float frame_ms = GetFrameTime() * 1000.f;
        SetWindowTitle(TextFormat("Dungeon WFC - %.2f ms/frame", frame_ms));

        // If the window is resized, adjust the render texture's size,
        // and update the destination rectangle to fit the new window size.
        if (IsWindowResized()) {
            // Get the new window size.
            width = GetScreenWidth();
            height = GetScreenHeight();

            // Ideally I would just center the board in the window,
            // and scale it to fit the smallest window dimension,
            // but for now I'll just force the window to be square.

            // Get the smallest window dimension.
            int min_size = width < height ? width : height;

            // Set the window size to be square with the window's smallest dimension.
            SetWindowSize(min_size, min_size);

            // Get the new window size again,
            // because It was just changed to be square.
            width = GetScreenWidth();
            height = GetScreenHeight();

            // Update the screen scale and destination rectangle.
            screen_scale = width / (float) BOARD_TEXTURE_WIDTH;
            dest = (Rectangle) { 0, 0, width, height };
        }

        // Start drawing to the render texture.
        BeginTextureMode(board_texture);
            // Draw the board to the render texture.
            BeginMode2D(board_camera);
                ClearBackground(RAYWHITE);
                draw_board();
            EndMode2D();
        EndTextureMode();

        // Start drawing to the window.
        BeginDrawing();
            ClearBackground(RAYWHITE);
            // Draw the render texture to the window.
            BeginMode2D(screen_camera);
                DrawTexturePro(board_texture.texture, source, dest, (Vector2) { 0, 0 }, 0, WHITE);
            EndMode2D();
        EndDrawing();

        // Handle user input.
        if (IsKeyPressed(KEY_R)) {
            reset_tiles();
            solve_board();
        }
    }

    free_textures();

    // Release the render texture and close the window.
    UnloadRenderTexture(board_texture);
    CloseWindow();

    return 0;
}
