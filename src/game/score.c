/**
 * \file game/score.c
 * \ingroup game_screens
 *
 * \brief
 * Implement the score screen, when the game is finished.
 */

#include "game/game.h"
#include "game/screens.h"

#include <SDL2/SDL.h>

static int on_event(struct oshu_game *game, union SDL_Event *event)
{
	switch (event->type) {
	case SDL_KEYDOWN:
		if (event->key.repeat)
			break;
		switch (event->key.keysym.sym) {
		case OSHU_QUIT_KEY:
			oshu_stop_game(game);
			break;
		}
		break;
	case SDL_WINDOWEVENT:
		switch (event->window.event) {
		case SDL_WINDOWEVENT_CLOSE:
			oshu_stop_game(game);
			break;
		}
		break;
	}
	return 0;
}

static int update(struct oshu_game *game)
{
	return 0;
}

static int draw(struct oshu_game *game)
{
	game->mode->draw(game);
	return 0;
}

struct oshu_game_screen oshu_score_screen = {
	.name = "Finished",
	.on_event = on_event,
	.update = update,
	.draw = draw,
};
