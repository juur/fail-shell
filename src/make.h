struct node {
	struct node *next;
	int type;
	union {
		int pad;
	} u;
};
