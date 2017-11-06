#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "types.h"
#include "geolocation.h"

typedef struct host type;

static long double distance(const int32_t place0[2], const int32_t place1[2]);

struct heap
{
	type *data; // array with the elements
	size_t count; // count of the elements in the heap
};

// Returns the biggest element in the heap
#define heap_front(h) (*(h)->data)

#define CMP(a, b) (distance(base, (a).coords) <= distance(base, (b).coords))

// Removes the biggest element from the heap
static void heap_pop(struct heap *restrict h, const int32_t base[2])
{
	ssize_t index, swap, other;

	// Remove the biggest element
	type temp = h->data[-(--h->count)];

	// Reorder the elements
	for(index = 0; 1; index = swap)
	{
		// Find the child to swap with
		swap = (index << 1) + 1;
		if (swap >= h->count) break; // If there are no children, the heap is reordered
		other = swap + 1;
		if ((other < h->count) && CMP(h->data[-other], h->data[-swap])) swap = other;
		if CMP(temp, h->data[-swap]) break; // If the bigger child is less than or equal to its parent, the heap is reordered

		h->data[-index] = h->data[-swap];
	}
	h->data[-index] = temp;
}

// Heapifies a non-empty array
// TODO: item assignment causes segmentation fault for count == 1
static void heapify(type *restrict data, size_t count, const int32_t base[2])
{
	ssize_t item, index, swap, other;
	type temp;

	// Move every non-leaf element to the right position in its subtree
	item = (count >> 1) - 1;
	while (1)
	{
		// Find the position of the current element in its subtree
		temp = data[-item];
		for(index = item; 1; index = swap)
		{
			// Find the child to swap with
			swap = (index << 1) + 1;
			if (swap >= count) break; // If there are no children, the current element is positioned
			other = swap + 1;
			if ((other < count) && CMP(data[-other], data[-swap])) swap = other;
			if CMP(temp, data[-swap]) break; // If the bigger child is smaller than or equal to the parent, the heap is reordered

			data[-index] = data[-swap];
		}
		if (index != item) data[-index] = temp;

		if (!item) return;
		--item;
	}
}

static const struct location *locate(uint32_t address)
{
	extern const struct location locations[];
	extern const size_t locations_count;

	// Binary search for location with the specified address
	size_t l = 0, r = locations_count;
	size_t index;
	const uint32_t *range;
	while (true)
	{
		index = (r - l) / 2 + l;
		range = locations[index].ip;

		if (range[0] <= address)
		{
			if (address <= range[1]) return (locations + index); // Found
			else l = index + 1;
		}
		else r = index;

		if (l == r) return 0; // Not found
	}
}

static long double distance(const int32_t place0[2], const int32_t place1[2])
{
	// 0 == Latitude
	// 1 == Longitude

	long double p0[2], p1[2];
	long double diff[2];

	// Convert angles to radians
	// Angles are stored in degrees multiplied by 10 000 in order to be stored as integers
	#define RAD(angle) ((angle) * (long double)M_PI / (10000 * 180.0L))
	p0[0] = RAD(place0[0]);
	p0[1] = RAD(place0[1]);
	p1[0] = RAD(place1[0]);
	p1[1] = RAD(place1[1]);
	#undef RAD

	// Calculate distance angle using the Haversine formula
	diff[0] = sinl((p0[0] - p1[0]) / 2);
	diff[1] = sinl((p0[1] - p1[1]) / 2);
	return 2 * asinl(sqrtl(diff[0] * diff[0] + cosl(p0[0]) * cosl(p1[0]) * diff[1] * diff[1]));
}

bool closest(uint32_t ip, struct host *restrict list, size_t length, size_t count)
{
	const struct location *loc = locate(ip);
	if (!loc) return false;
	const int32_t *base = loc->coords;

	struct heap heap;
	heap.count = length;
	heap.data = list + length - 1;

	heapify(heap.data, length, base);

	type host;
	size_t i;
	for(i = 0; i < count; ++i)
	{
		host = heap_front(&heap);
		heap_pop(&heap, base);
		list[i] = host;
	}

	return true;
}
