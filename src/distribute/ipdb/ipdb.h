struct location
{
	uint32_t ip[2];			// low, high
	int32_t coords[2];		// latitude, longitude
	unsigned char country;
};

//extern const struct location locations[];

/*char *countries[] = {
	"Bulgaria",
	"Germany",
	"USA",
};*/
