struct node {
	struct node *next;
	int type;
	union {
		unsigned long tlong;
	} u;
};
