void bench_generated_exercise();
void bench_baseline_exercise();

int main() {
    // exercise both suites to force actual compilation in both TUs
    bench_generated_exercise();
    bench_baseline_exercise();
    return 0;
}
