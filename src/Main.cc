// Varkor Version: 3af75a0

#include <AssetLibrary.h>
#include <Error.h>
#include <Input.h>
#include <Registrar.h>
#include <Temporal.h>
#include <VarkorMain.h>
#include <comp/AlphaColor.h>
#include <comp/Camera.h>
#include <comp/Sprite.h>
#include <comp/Text.h>
#include <comp/Transform.h>
#include <ds/Vector.h>
#include <editor/Editor.h>
#include <gfx/Shader.h>
#include <world/Object.h>
#include <world/World.h>

namespace Assets {
AssetId nSpriteColorShader;
void Initialize() {
  nSpriteColorShader = AssLib::CreateInit<Gfx::Shader>(
      "SpriteColor", "vres/shader/sprite.vs", "vres/shader/color.fs");
}
} // namespace Assets

// Component used for flashing a single color sprite.
struct Flash {
  float mDuration;
  float mStartTime;
  void VInit(const World::Object &owner) {
    Comp::Sprite &spriteComp = owner.Get<Comp::Sprite>();
    spriteComp.mShaderId = Assets::nSpriteColorShader;
    Comp::AlphaColor &colorComp = owner.Get<Comp::AlphaColor>();
    colorComp.mColor = {1.0f, 1.0f, 1.0f, 1.0f};
    mStartTime = Temporal::TotalTime();
  }

  void VUpdate(const World::Object &owner) {
    float timeSince = Temporal::TotalTime() - mStartTime;
    if (timeSince > mDuration) {
      owner.Delete();
      return;
    }
    Comp::AlphaColor &colorComp = owner.Get<Comp::AlphaColor>();
    float fade = 1.0f - (timeSince / mDuration);
    for (int i = 0; i < 4; ++i) {
      colorComp.mColor[i] = fade;
    }
  }
};

enum class Tetrimino { I, L, J, O, S, T, Z, None };

// These define the cells that are visible.
#define GRID_HEIGHT 20
#define GRID_WIDTH 10

// In actuallity, the grid is slightly larger than the visible grid. Pieces are
// spawned into the grid in this invisible region.
#define FULL_GRID_HEIGHT 22

struct Cell {
  Tetrimino mTetriminoType;
  World::MemberId mMemberId;

  // This indicates whether a locked tetrimino occupies a cell. A row of locked
  // cells gets eliminated.
  bool mLocked;
};

struct Tetris {
  // All of the tetrimino shapes are stored here.
  int mShapes[7][4][4];

  // The number of invisible rows.
  int mVisibleRowOffset;

  // The full grid that the game is played on.
  Cell mGrid[FULL_GRID_HEIGHT][GRID_WIDTH];

  // The queue of upcoming tetriminos.
  Tetrimino mTetrimoQueue[3];

  // The members for the sprites that display the queue.
  World::MemberId mQueueCellMemberId[3][4][4];
  // The members for the parents of the queue sprites. This allows us to move
  // each set of queue sprites as a group.
  World::MemberId mQueueCellParents[3];

  // All the different members used for displaying text.
  World::MemberId mLinesTextMemberId;
  World::MemberId mRateTextMemberId;
  World::MemberId mStartGameTextMemberId;
  World::MemberId mEndGameTextMemberId;

  // Values for specifying where and what the active tetrimino is.
  Tetrimino mActiveTetrimino;
  int mShapeRotation;
  int mActiveX;
  int mActiveY;

  // Values used for controlling vertical dropping.
  float mDropRate;
  float mFastDropRate;
  float mTimeSinceLastDrop;

  // Values used for controlling horizontal shifting.
  float mShiftRate;
  float mTimeSinceLastShift;

  // The number of completed lines.
  int mLines;

  // Keep track of whether the game is running.
  bool mRunning;

  void ClearGrid() {
    for (int i = 0; i < FULL_GRID_HEIGHT; ++i) {
      for (int j = 0; j < GRID_WIDTH; ++j) {
        Cell &cell = mGrid[i][j];
        cell.mLocked = false;
        cell.mTetriminoType = Tetrimino::None;
      }
    }
  }

  void VInit(const World::Object &owner) {
    // Initialize the different Tetrmino shapes.
    // clang-format off
    int shapes[7][4][4] = {
      {{0, 1, 0, 0},
       {0, 1, 0, 0},
       {0, 1, 0, 0},
       {0, 1, 0, 0}},

      {{0, 1, 0, 0},
       {0, 1, 0, 0},
       {0, 1, 1, 0},
       {0, 0, 0, 0}},

      {{0, 0, 1, 0},
       {0, 0, 1, 0},
       {0, 1, 1, 0},
       {0, 0, 0, 0}},

      {{0, 0, 0, 0},
       {0, 1, 1, 0},
       {0, 1, 1, 0},
       {0, 0, 0, 0}},

      {{0, 0, 0, 0},
       {0, 1, 1, 0},
       {1, 1, 0, 0},
       {0, 0, 0, 0}},

      {{0, 0, 0, 0},
       {0, 1, 0, 0},
       {1, 1, 1, 0},
       {0, 0, 0, 0}},

      {{0, 0, 0, 0},
       {0, 1, 1, 0},
       {0, 0, 1, 1},
       {0, 0, 0, 0}}};
    // clang-format on
    for (int i = 0; i < 7; ++i) {
      for (int j = 0; j < 4; ++j) {
        for (int k = 0; k < 4; ++k) {
          mShapes[i][j][k] = shapes[i][j][k];
        }
      }
    }

    // Create all of the sprites to represent the grid.
    mVisibleRowOffset = FULL_GRID_HEIGHT - GRID_HEIGHT;
    ClearGrid();
    for (int i = 0; i < GRID_HEIGHT; ++i) {
      for (int j = 0; j < GRID_WIDTH; ++j) {
        Cell &cell = mGrid[i + mVisibleRowOffset][j];
        cell.mMemberId = owner.mSpace->CreateMember();
        World::Object cellObject(owner.mSpace, cell.mMemberId);
        Comp::Transform &cellTrans = cellObject.Add<Comp::Transform>();
        Vec3 offset = {-(float)(GRID_WIDTH / 2), (float)(GRID_HEIGHT / 2),
                       0.0f};
        Vec3 translation = {(float)j, -(float)i, 0.0f};
        translation += offset;
        cellTrans.SetTranslation(translation);
        cellTrans.SetUniformScale(0.9f);

        Comp::Sprite &cellSprite = cellObject.Add<Comp::Sprite>();
        cellSprite.mShaderId = Assets::nSpriteColorShader;
        cellObject.Add<Comp::AlphaColor>();
      }
    }
    UpdateColors(owner);

    // Initialize the tetrimino queue.
    for (int i = 0; i < 3; ++i) {
      mTetrimoQueue[i] = Tetrimino(rand() % (int)Tetrimino::None);
    }

    // Create the sprites that represent the tetrimino queue.
    for (int i = 0; i < 3; ++i) {
      mQueueCellParents[i] = owner.mSpace->CreateMember();
      for (int j = 0; j < 4; ++j) {
        for (int k = 0; k < 4; ++k) {
          mQueueCellMemberId[i][j][k] =
              owner.mSpace->CreateChildMember(mQueueCellParents[i]);
          Comp::Sprite &sprite =
              owner.mSpace->Add<Comp::Sprite>(mQueueCellMemberId[i][j][k]);
          sprite.mShaderId = Assets::nSpriteColorShader;
          owner.mSpace->Add<Comp::AlphaColor>(mQueueCellMemberId[i][j][k]);
          Comp::Transform &transform =
              owner.mSpace->Get<Comp::Transform>(mQueueCellMemberId[i][j][k]);
          transform.SetTranslation({(float)k, -(float)j, 0.0f});
          transform.SetUniformScale(0.9f);
        }
      }
    }
    UpdateQueueColors(owner);

    // Position the queue parents.
    Comp::Transform &queueParentTrans0 =
        owner.mSpace->Add<Comp::Transform>(mQueueCellParents[0]);
    queueParentTrans0.SetTranslation({(float)GRID_WIDTH / 2.0f + 2.0f,
                                      (float)GRID_HEIGHT / 2.0f - 4.5f, 0.0f});
    Comp::Transform &queueParentTrans1 =
        owner.mSpace->Add<Comp::Transform>(mQueueCellParents[1]);
    queueParentTrans1.SetTranslation({(float)GRID_WIDTH / 2.0f + 2.0f,
                                      (float)GRID_HEIGHT / 2.0f - 8.5f, 0.0f});
    queueParentTrans1.SetUniformScale(0.7f);
    Comp::Transform &queueParentTrans2 =
        owner.mSpace->Add<Comp::Transform>(mQueueCellParents[2]);
    queueParentTrans2.SetTranslation({(float)GRID_WIDTH / 2.0f + 2.0f,
                                      (float)GRID_HEIGHT / 2.0f - 11.5f, 0.0f});
    queueParentTrans2.SetUniformScale(0.7f);

    // Create the score text.
    mLinesTextMemberId = owner.mSpace->CreateMember();
    Comp::Text &linesTextComp =
        owner.mSpace->Add<Comp::Text>(mLinesTextMemberId);
    linesTextComp.mText = "Lines: 0";
    linesTextComp.mAlign = Comp::Text::Alignment::Left;
    linesTextComp.mWidth = 10.0f;
    Comp::Transform &linesTrans =
        owner.mSpace->Get<Comp::Transform>(mLinesTextMemberId);
    Vec3 translation = {(float)(GRID_WIDTH / 2) + 1.0f +
                            linesTextComp.mWidth / 2.0f,
                        (float)(GRID_HEIGHT / 2) - 1.0f, 0.0f};
    linesTrans.SetTranslation(translation);
    Comp::AlphaColor &linesColorComp =
        owner.mSpace->Add<Comp::AlphaColor>(mLinesTextMemberId);
    linesColorComp.mColor = {1.0f, 1.0f, 1.0f, 1.0f};

    // Create the rate text.
    mRateTextMemberId = owner.mSpace->CreateMember();
    Comp::Text &rateTextComp = owner.mSpace->Add<Comp::Text>(mRateTextMemberId);
    rateTextComp.mText = "Rate: 1.0";
    rateTextComp.mAlign = Comp::Text::Alignment::Left;
    rateTextComp.mWidth = 10.0f;
    Comp::Transform &rateTrans =
        owner.mSpace->Get<Comp::Transform>(mRateTextMemberId);
    rateTrans.SetTranslation(
        {(float)(GRID_WIDTH / 2) + 1.0f + rateTextComp.mWidth / 2.0f,
         (float)(GRID_HEIGHT / 2) - 3.0f, 0.0f});
    Comp::AlphaColor &rateColorComp =
        owner.mSpace->Add<Comp::AlphaColor>(mRateTextMemberId);
    rateColorComp.mColor = {1.0f, 1.0f, 1.0f, 1.0f};

    // Create the start game text.
    mStartGameTextMemberId = owner.mSpace->CreateMember();
    Comp::Text &startGameTextComp =
        owner.mSpace->Add<Comp::Text>(mStartGameTextMemberId);
    startGameTextComp.mAlign = Comp::Text::Alignment::Center;
    startGameTextComp.mText = "Press the down arrow to play.";
    Comp::Transform &startGameTransComp =
        owner.mSpace->Get<Comp::Transform>(mStartGameTextMemberId);
    startGameTransComp.SetTranslation({-0.5f, 1.0f, 0.5f});
    startGameTransComp.SetUniformScale(1.2f);
    Comp::AlphaColor &startGameColorComp =
        owner.mSpace->Add<Comp::AlphaColor>(mStartGameTextMemberId);
    startGameColorComp.mColor = {1.0f, 1.0f, 1.0f, 1.0f};

    // Create the end game text
    mEndGameTextMemberId = owner.mSpace->CreateMember();
    Comp::Text &endGameTextComp =
        owner.mSpace->Add<Comp::Text>(mEndGameTextMemberId);
    endGameTextComp.mAlign = Comp::Text::Alignment::Center;
    endGameTextComp.mText =
        "You topped off. Press the down arrow to play again.";
    endGameTextComp.mVisible = false;
    Comp::Transform &endGameTransComp =
        owner.mSpace->Get<Comp::Transform>(mEndGameTextMemberId);
    endGameTransComp.SetTranslation({-0.5f, 2.0f, 0.5f});
    endGameTransComp.SetUniformScale(1.2f);
    Comp::AlphaColor &endGameColorComp =
        owner.mSpace->Add<Comp::AlphaColor>(mEndGameTextMemberId);
    endGameColorComp.mColor = {1.0f, 1.0f, 1.0f, 1.0f};

    mActiveTetrimino = Tetrimino::None;

    mDropRate = 1.0f;
    mFastDropRate = 20.0f;
    mTimeSinceLastDrop = 0.0f;

    mShiftRate = 10.0f;
    mTimeSinceLastShift = 1.0f / mShiftRate;

    mRunning = false;

    // Create the camera that the game will be rendered with.
    World::MemberId cameraMemberId = owner.mSpace->CreateMember();
    Comp::Camera &camera = owner.mSpace->Add<Comp::Camera>(cameraMemberId);
    camera.mProjectionType = Comp::Camera::ProjectionType::Orthographic;
    camera.mHeight = (float)GRID_HEIGHT + 2.0f;
    Comp::Transform &cameraTrans =
        owner.mSpace->Get<Comp::Transform>(cameraMemberId);
    cameraTrans.SetTranslation({0.0f, 0.5f, 1.0f});
    owner.mSpace->mCameraId = cameraMemberId;
  }

  void ClearUnlockedCells() {
    for (int i = 0; i < FULL_GRID_HEIGHT; ++i) {
      for (int j = 0; j < GRID_WIDTH; ++j) {
        if (!mGrid[i][j].mLocked) {
          mGrid[i][j].mTetriminoType = Tetrimino::None;
        }
      }
    }
  }

  Vec4 GetTetriminoColor(Tetrimino tetrimino) {
    switch (tetrimino) {
    case Tetrimino::I:
      return {0.0f, 1.0f, 1.0f, 1.0f};
    case Tetrimino::L:
      return {1.0f, 0.5f, 0.0f, 1.0f};
    case Tetrimino::J:
      return {0.0f, 0.0f, 1.0f, 1.0f};
    case Tetrimino::O:
      return {1.0f, 1.0f, 0.0f, 1.0f};
    case Tetrimino::S:
      return {0.0f, 1.0f, 0.0f, 1.0f};
    case Tetrimino::T:
      return {1.0f, 0.0f, 1.0f, 1.0f};
    case Tetrimino::Z:
      return {1.0f, 0.0f, 0.0f, 1.0f};
    }
    return {0.5f, 0.5f, 0.5f, 1.0f};
  }

  void UpdateColors(const World::Object &owner) {
    for (int i = 0; i < GRID_HEIGHT; ++i) {
      for (int j = 0; j < GRID_WIDTH; ++j) {
        Cell &cell = mGrid[i + mVisibleRowOffset][j];
        Comp::AlphaColor &colorComp =
            owner.mSpace->Get<Comp::AlphaColor>(cell.mMemberId);
        colorComp.mColor = GetTetriminoColor(cell.mTetriminoType);
      }
    }
  }

  void UpdateQueueColors(const World::Object &owner) {
    for (int i = 0; i < 3; ++i) {
      for (int j = 0; j < 4; ++j) {
        for (int k = 0; k < 4; ++k) {
          Comp::AlphaColor &colorComp =
              owner.mSpace->Get<Comp::AlphaColor>(mQueueCellMemberId[i][j][k]);
          if (mShapes[(int)mTetrimoQueue[i]][j][k] == 1) {
            colorComp.mColor = GetTetriminoColor(mTetrimoQueue[i]);
          } else {
            colorComp.mColor = GetTetriminoColor(Tetrimino::None);
          }
        }
      }
    }
  }

  void CreateRowFlash(const World::Object &owner, int row) {
    World::MemberId flashId = owner.mSpace->CreateMember();
    Flash &flashComp = owner.mSpace->Add<Flash>(flashId);
    flashComp.mDuration = 0.5f;
    Comp::Transform &flashTrans = owner.mSpace->Get<Comp::Transform>(flashId);
    float height = (float)GRID_HEIGHT / 2.0f - (float)(row - mVisibleRowOffset);
    Vec3 translation = {-0.5f, height, 0.5f};
    flashTrans.SetTranslation(translation);
    flashTrans.SetScale({(float)GRID_WIDTH, 1.0f, 1.0f});
  }

  void LockActiveTetrimino(const World::Object &owner, int shape[4][4]) {
    // Lock the cells that the active tetrimino occupies.
    for (int i = 0; i < 4; ++i) {
      for (int j = 0; j < 4; ++j) {
        Cell &cell = mGrid[mActiveY + i][mActiveX + j];
        if (shape[i][j] == 1) {
          // The game is over if part of the shape is above the visible grid.
          if (mActiveY + i < mVisibleRowOffset) {
            EndGame(owner);
          }
          cell.mTetriminoType = mActiveTetrimino;
          cell.mLocked = true;
        }
      }
    }

    // Delete complete rows.
    int collapseDistance = 0;
    for (int i = FULL_GRID_HEIGHT - 1; i >= 0; --i) {
      bool deleteRow = true;
      for (int j = 0; j < GRID_WIDTH; ++j) {
        if (mGrid[i][j].mLocked != true) {
          deleteRow = false;
          break;
        }
      }
      if (deleteRow) {
        CreateRowFlash(owner, i);
        ++collapseDistance;
        continue;
      }
      if (collapseDistance == 0) {
        continue;
      }
      for (int j = 0; j < GRID_WIDTH; ++j) {
        Cell &toCell = mGrid[i + collapseDistance][j];
        Cell &fromCell = mGrid[i][j];
        toCell.mLocked = fromCell.mLocked;
        toCell.mTetriminoType = fromCell.mTetriminoType;
        fromCell.mLocked = false;
        fromCell.mTetriminoType = Tetrimino::None;
      }
    }
    mActiveTetrimino = Tetrimino::None;

    // Update the score and increase the drop rate if a threshold is passed.
    if (collapseDistance == 0) {
      return;
    }
    bool increaseRate = false;
    for (int i = 1; i <= collapseDistance; ++i) {
      if ((mLines + i) % 10 == 0 && mLines > 0) {
        mDropRate += 1.0f;
        UpdateRateText(owner);
      }
    }
    mLines += collapseDistance;
    UpdateLinesText(owner);
  }

  void GetRotatedShape(int shape[4][4], Tetrimino tetrimino, int amount) {
    for (int i = 0; i < 4; ++i) {
      for (int j = 0; j < 4; ++j) {
        shape[i][j] = mShapes[(int)tetrimino][i][j];
      }
    }
    for (int i = 0; i < amount; ++i) {
      int newShape[4][4];
      for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
          newShape[i][j] = shape[j][3 - i];
        }
      }
      for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
          shape[i][j] = newShape[i][j];
        }
      }
    }
  }

  bool CanMoveShape(int shape[4][4], int x, int y) {
    int newX = mActiveX + x;
    int newY = mActiveY + y;
    for (int i = 0; i < 4; ++i) {
      for (int j = 0; j < 4; ++j) {
        if (shape[i][j] != 1) {
          continue;
        }
        // Is it within the grid bounds?
        if (newX + j >= GRID_WIDTH || newX + j < 0) {
          return false;
        }
        if (newY + i >= FULL_GRID_HEIGHT) {
          return false;
        }
        // Does it collide with an already locked cell?
        Cell &cell = mGrid[newY + i][newX + j];
        if (cell.mLocked) {
          return false;
        }
      }
    }
    return true;
  }

  void HandleRotation(int shape[4][4]) {
    // Update the rotation value depending on input.
    int oldRotation = mShapeRotation;
    int newRotation = mShapeRotation;
    if (Input::KeyPressed(Input::Key::T)) {
      newRotation++;
      if (newRotation == 4) {
        newRotation = 0;
      }
    }
    if (Input::KeyPressed(Input::Key::R)) {
      newRotation--;
      if (newRotation == -1) {
        newRotation = 3;
      }
    }
    mShapeRotation = newRotation;

    // Check that the new rotation is possible. If it isn't, see if the shape
    // fits after being kicked to an orthogonally adjacent position and cancel
    // the rotation if it doesn't.
    GetRotatedShape(shape, mActiveTetrimino, mShapeRotation);
    if (newRotation != oldRotation && !CanMoveShape(shape, 0, 0)) {
      if (CanMoveShape(shape, 1, 0)) {
        mActiveX++;
      } else if (CanMoveShape(shape, 0, 1)) {
        mActiveY++;
      } else if (CanMoveShape(shape, -1, 0)) {
        mActiveX--;
      } else if (CanMoveShape(shape, 0, -1)) {
        mActiveY--;
      } else {
        mShapeRotation = oldRotation;
        GetRotatedShape(shape, mActiveTetrimino, mShapeRotation);
      }
    }
  }

  void HandleHorizontalShift(int shape[4][4]) {
    // Determine whether a shift is ready depending on the shift rate.
    float shiftTimeGap = 1.0f / mShiftRate;
    if (Input::KeyDown(Input::Key::Left) || Input::KeyDown(Input::Key::Right)) {
      mTimeSinceLastShift += Temporal::DeltaTime();
    }
    bool shiftBereit = mTimeSinceLastShift >= shiftTimeGap;

    // Perform shifts if they are possible depending on input.
    if (Input::KeyDown(Input::Key::Left) && shiftBereit) {
      bool canMove = CanMoveShape(shape, -1, 0);
      if (canMove) {
        mActiveX--;
      }
      mTimeSinceLastShift -= shiftTimeGap;
    }
    if (Input::KeyDown(Input::Key::Right) && shiftBereit) {
      bool canMove = CanMoveShape(shape, 1, 0);
      if (canMove) {
        mActiveX++;
      }
      mTimeSinceLastShift -= shiftTimeGap;
    }

    // Reset the time since last shift so that a shift instantly happens when
    // the left or right arrow is pressed again.
    if (Input::KeyReleased(Input::Key::Right) ||
        Input::KeyReleased(Input::Key::Left)) {
      mTimeSinceLastShift = shiftTimeGap;
    }
  }

  void HandleDrop(const World::Object &owner, int shape[4][4]) {
    float dropRate = mDropRate;
    if (Input::KeyDown(Input::Key::Down)) {
      dropRate = mFastDropRate;
    }
    float dropTimeGap = 1.0f / dropRate;
    mTimeSinceLastDrop += Temporal::DeltaTime();
    if (mTimeSinceLastDrop > dropTimeGap) {
      bool canDrop = CanMoveShape(shape, 0, 1);
      if (!canDrop) {
        LockActiveTetrimino(owner, shape);
      } else {
        mActiveY++;
      }
      while (mTimeSinceLastDrop > dropTimeGap) {
        mTimeSinceLastDrop -= dropTimeGap;
      }
    }
  }

  void EndGame(const World::Object &owner) {
    Comp::Text &endGameTextComp =
        owner.mSpace->Get<Comp::Text>(mEndGameTextMemberId);
    endGameTextComp.mVisible = true;
    mRunning = false;
  }

  void SpawnTetrimino(const World::Object &owner) {
    mShapeRotation = 0;
    mActiveTetrimino = mTetrimoQueue[0];
    mActiveX = GRID_WIDTH / 2 - 2;
    mActiveY = 0;

    // Handle changes to the tetrimino queue.
    for (int i = 1; i < 3; ++i) {
      mTetrimoQueue[i - 1] = mTetrimoQueue[i];
    }
    srand((unsigned int)Temporal::TotalTime());
    mTetrimoQueue[2] = Tetrimino(rand() % (int)Tetrimino::None);
    UpdateQueueColors(owner);

    // The game is over if the tetrimino spawns on any locked cells.
    for (int i = 0; i < 4; ++i) {
      for (int j = 0; j < 4; ++j) {
        const Cell &cell = mGrid[mActiveY + i][mActiveX + j];
        if (mShapes[(int)mActiveTetrimino][i][j] == 1 && cell.mLocked) {
          EndGame(owner);
        }
      }
    }
  }

  void UpdateLinesText(const World::Object &owner) {
    Comp::Text &linesTextComp =
        owner.mSpace->Get<Comp::Text>(mLinesTextMemberId);
    std::stringstream lineText;
    lineText << "Lines: " << mLines;
    linesTextComp.mText = lineText.str();
  }

  void UpdateRateText(const World::Object &owner) {
    Comp::Text &rateTextComp = owner.mSpace->Get<Comp::Text>(mRateTextMemberId);
    std::stringstream rateText;
    rateText << "Rate: " << mDropRate;
    rateTextComp.mText = rateText.str();
  }

  void StartGame(const World::Object &owner) {
    // Hide the start and end game text elements.
    Comp::Text &startGameTextComp =
        owner.mSpace->Get<Comp::Text>(mStartGameTextMemberId);
    startGameTextComp.mVisible = false;
    Comp::Text &endGameTextComp =
        owner.mSpace->Get<Comp::Text>(mEndGameTextMemberId);
    endGameTextComp.mVisible = false;

    ClearGrid();
    mLines = 0;
    UpdateLinesText(owner);
    mDropRate = 1.0f;
    UpdateRateText(owner);
    mRunning = true;
  }

  void VUpdate(const World::Object &owner) {
    if (!mRunning) {
      if (Input::KeyPressed(Input::Key::Down)) {
        StartGame(owner);
      }
      return;
    }

    ClearUnlockedCells();

    if (mActiveTetrimino == Tetrimino::None) {
      SpawnTetrimino(owner);
    }

    int shape[4][4];
    HandleRotation(shape);
    HandleHorizontalShift(shape);
    HandleDrop(owner, shape);

    // Color in the cells of the active tetrimino type. We need to check that
    // the active tetrimino isn't set to None because it will change to None
    // when a piece is locked.
    if (mActiveTetrimino != Tetrimino::None) {
      for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
          int cellY = mActiveY + i;
          int cellX = mActiveX + j;
          Cell &cell = mGrid[cellY][cellX];
          if (shape[i][j] == 1) {
            cell.mTetriminoType = mActiveTetrimino;
          }
        }
      }
    }

    UpdateColors(owner);
  }
};

void CustomRegistrar() {
  Registrar::Register<Tetris>();
  Registrar::Register<Flash, Comp::Sprite, Comp::AlphaColor>();
}

int WinMain(void) {
  Registrar::nRegisterCustomTypes = CustomRegistrar;
  Result result = VarkorInit(__argc, __argv, "Tetris", PROJECT_DIRECTORY);
  LogAbortIf(!result.Success(), result.mError.c_str());

  Editor::nEditorMode = false;
  Editor::nHideInterface = true;
  World::nPause = false;
  Assets::Initialize();
  World::SpaceIt spaceIt = World::CreateTopSpace();
  World::MemberId tetrisMember = spaceIt->CreateMember();
  spaceIt->Add<Tetris>(tetrisMember);

  VarkorRun();
  VarkorPurge();
}
