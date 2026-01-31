#ifndef SDL_CRT_ROYALE_MASKS_H
#define SDL_CRT_ROYALE_MASKS_H

typedef struct {
	const unsigned char *data;
	int width;
	int height;
} CRT_RoyaleMask;

typedef enum {
	CRT_ROYALE_MASK_GRILLE_SMALL = 0,
	CRT_ROYALE_MASK_GRILLE_LARGE = 1,
	CRT_ROYALE_MASK_SLOT_SMALL = 2,
	CRT_ROYALE_MASK_SLOT_LARGE = 3,
	CRT_ROYALE_MASK_SHADOW_SMALL = 4,
	CRT_ROYALE_MASK_SHADOW_LARGE = 5,
	CRT_ROYALE_MASK_COUNT
} CRT_RoyaleMaskId;

extern const CRT_RoyaleMask CRT_ROYALE_masks[CRT_ROYALE_MASK_COUNT];

#endif
