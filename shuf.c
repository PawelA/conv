extern int rand(void);

void shuffle(int *arr, int n) {
	int p, t;
	while (n > 1) {
		p = rand() % n;
		t = arr[p];
		n--;
		arr[p] = arr[n];
		arr[n] = t;
	}
}
