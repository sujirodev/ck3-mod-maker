// Dijkstra on a 120x120 grid with a binary heap, repeated n times (similar to the cost of a pathfinder)
#define BW 120
#define BN (BW * BW)
static int bench_dist[BN], bench_heap[BN * 4], bench_hd[BN * 4];
static int bench(int n)
{
	int total = 0;
	for (int rep = 0; rep < n; rep++) {
		for (int i = 0; i < BN; i++)
			bench_dist[i] = 1 << 30;
		int hn = 0, src = rep % BN;
		bench_dist[src] = 0;
		bench_heap[hn] = src; bench_hd[hn++] = 0;
		while (hn) {
			int v = bench_heap[0], d = bench_hd[0];
			hn--;
			bench_heap[0] = bench_heap[hn]; bench_hd[0] = bench_hd[hn];
			for (int k = 0;;) {
				int l = 2 * k + 1, r = l + 1, m = k;
				if (l < hn && bench_hd[l] < bench_hd[m]) m = l;
				if (r < hn && bench_hd[r] < bench_hd[m]) m = r;
				if (m == k) break;
				int t = bench_heap[k], td = bench_hd[k];
				bench_heap[k] = bench_heap[m]; bench_hd[k] = bench_hd[m];
				bench_heap[m] = t; bench_hd[m] = td;
				k = m;
			}
			if (d > bench_dist[v]) continue;
			int x = v % BW, y = v / BW;
			int nb[4] = { x > 0 ? v - 1 : -1, x < BW - 1 ? v + 1 : -1, y > 0 ? v - BW : -1, y < BW - 1 ? v + BW : -1 };
			for (int e = 0; e < 4; e++) {
				int u = nb[e];
				if (u < 0) continue;
				int nd = d + 1 + ((u * 7 + v * 13) & 7);
				if (nd < bench_dist[u] && hn < BN * 4) {
					bench_dist[u] = nd;
					int k = hn++;
					bench_heap[k] = u; bench_hd[k] = nd;
					while (k > 0 && bench_hd[(k - 1) / 2] > bench_hd[k]) {
						int p = (k - 1) / 2, t = bench_heap[k], td = bench_hd[k];
						bench_heap[k] = bench_heap[p]; bench_hd[k] = bench_hd[p];
						bench_heap[p] = t; bench_hd[p] = td;
						k = p;
					}
				}
			}
		}
		total += bench_dist[BN - 1 - src];
	}
	return total;
}
