// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

using System;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Runtime.CompilerServices;
using Xunit;
using Xunit.Abstractions;

namespace Microsoft.ML.OnnxRuntimeGenAI.Tests
{
    public class OnnxRuntimeGenAITests
    {
        private readonly ITestOutputHelper output;

        private static string GetDirectoryInTreeThatContains(string currentDirectory, string targetDirectoryName)
        {
            bool found = false;
            foreach (string d in Directory.GetDirectories(currentDirectory, searchPattern: targetDirectoryName))
            {
                found = true;
                return Path.Combine(currentDirectory, targetDirectoryName);
            }
            if (!found)
            {
                DirectoryInfo dirInfo = new DirectoryInfo(currentDirectory);
                if (dirInfo.Parent != null)
                {
                    return GetDirectoryInTreeThatContains(Path.GetFullPath(Path.Combine(currentDirectory, "..")), targetDirectoryName);
                }
                else
                {
                    return null;
                }
            }
            return null;
        }

        private static readonly string _phi2Path = Path.Combine(
            GetDirectoryInTreeThatContains(Directory.GetCurrentDirectory(), "test"), "test_models", "phi-2", "int4", "cpu");

        public OnnxRuntimeGenAITests(ITestOutputHelper o)
        {
            this.output = o;
        }

        private class IgnoreOnModelAbsenceFact : FactAttribute
        {
            public IgnoreOnModelAbsenceFact()
            {
                string modelPath = _phi2Path;
                bool exists = System.IO.Directory.Exists(modelPath);
                if (!System.IO.Directory.Exists(modelPath))
                {
                    // Skip this test on some machines since the model cannot be downloaded on those machines at runtime.
                    Skip = "Skipping this test since the model does not exist.";
                }
            }
        }

        [Fact(DisplayName = "TestConfig")]
        public void TestConfig()
        {
            string modelPath = Path.Combine(Directory.GetCurrentDirectory(), "test_models", "hf-internal-testing", "tiny-random-gpt2-fp32");
            using (var config = new Config(modelPath))
            {
                config.ClearProviders();
                config.SetProviderOption("cuda", "device_id", "0");
                config.SetProviderOption("cuda", "catch_fire", "false");
                config.AppendProvider("pigeon");
                // At this point the providers is 'cuda' first and 'pigeon' as secondary.
                // Given some provider options are made up and there is no pigeon provider, the model won't load.
                // This tests the API
            }
        }

        [Fact(DisplayName = "TestGreedySearch")]
        public void TestGreedySearch()
        {
            ulong maxLength = 10;
            int[] inputIDs = new int[] { 0, 0, 0, 52, 0, 0, 195, 731 };
            var inputIDsShape = new ulong[] { 2, 4 };
            ulong batchSize = inputIDsShape[0];
            ulong sequenceLength = inputIDsShape[1];
            var expectedOutput = new int[] { 0, 0, 0, 52, 204, 204, 204, 204, 204, 204,
                                             0, 0, 195, 731, 731, 114, 114, 114, 114, 114 };

            string modelPath = Path.Combine(Directory.GetCurrentDirectory(), "test_models", "hf-internal-testing", "tiny-random-gpt2-fp32");
            using (var config = new Config(modelPath))
            {
                Assert.NotNull(config);
                using (var model = new Model(config))
                {
                    using (var generatorParams = new GeneratorParams(model))
                    {
                        Assert.NotNull(generatorParams);

                        generatorParams.SetSearchOption("max_length", maxLength);
                        generatorParams.SetInputIDs(inputIDs, sequenceLength, batchSize);

                        using (var generator = new Generator(model, generatorParams))
                        {
                            Assert.NotNull(generator);

                            while (!generator.IsDone())
                            {
                                generator.ComputeLogits();
                                generator.GenerateNextToken();
                            }

                            for (ulong i = 0; i < batchSize; i++)
                            {
                                var sequence = generator.GetSequence(i).ToArray();
                                var expectedSequence = expectedOutput.Skip((int)i * (int)maxLength).Take((int)maxLength);
                                Assert.Equal(expectedSequence, sequence);
                            }
                        }

                        var sequences = model.Generate(generatorParams);
                        Assert.NotNull(sequences);

                        for (ulong i = 0; i < batchSize; i++)
                        {
                            var expectedSequence = expectedOutput.Skip((int)i * (int)maxLength).Take((int)maxLength);
                            Assert.Equal(expectedSequence, sequences[i].ToArray());
                        }
                    }
                }
            }
        }

        [IgnoreOnModelAbsenceFact(DisplayName = "TestTopKSearch")]
        public void TestTopKSearch()
        {
            int topK = 100;
            float temp = 0.6f;
            ulong maxLength = 20;

            string modelPath = _phi2Path;
            using (var model = new Model(modelPath))
            {
                Assert.NotNull(model);
                using (var tokenizer = new Tokenizer(model))
                {
                    Assert.NotNull(tokenizer);

                    var strings = new string[] {
                        "This is a test.",
                        "Rats are awesome pets!",
                        "The quick brown fox jumps over the lazy dog."
                    };

                    var sequences = tokenizer.EncodeBatch(strings);
                    Assert.NotNull(sequences);
                    Assert.Equal((ulong)strings.Length, sequences.NumSequences);

                    using GeneratorParams generatorParams = new GeneratorParams(model);
                    Assert.NotNull(generatorParams);

                    generatorParams.SetInputSequences(sequences);
                    generatorParams.SetSearchOption("max_length", maxLength);
                    generatorParams.SetSearchOption("do_sample", true);
                    generatorParams.SetSearchOption("top_k", topK);
                    generatorParams.SetSearchOption("temperature", temp);
                    var outputSequences = model.Generate(generatorParams);
                    Assert.NotNull(outputSequences);

                    var outputStrings = tokenizer.DecodeBatch(outputSequences);
                    Assert.NotNull(outputStrings);
                }
            }
        }

        [IgnoreOnModelAbsenceFact(DisplayName = "TestTopPSearch")]
        public void TestTopPSearch()
        {
            float topP = 0.6f;
            float temp = 0.6f;
            ulong maxLength = 20;

            string modelPath = _phi2Path;
            using (var model = new Model(modelPath))
            {
                Assert.NotNull(model);
                using (var tokenizer = new Tokenizer(model))
                {
                    Assert.NotNull(tokenizer);

                    var strings = new string[] {
                        "This is a test.",
                        "Rats are awesome pets!",
                        "The quick brown fox jumps over the lazy dog."
                    };

                    var sequences = tokenizer.EncodeBatch(strings);
                    Assert.NotNull(sequences);
                    Assert.Equal((ulong)strings.Length, sequences.NumSequences);

                    using GeneratorParams generatorParams = new GeneratorParams(model);
                    Assert.NotNull(generatorParams);

                    generatorParams.SetInputSequences(sequences);
                    generatorParams.SetSearchOption("max_length", maxLength);
                    generatorParams.SetSearchOption("do_sample", true);
                    generatorParams.SetSearchOption("top_p", topP);
                    generatorParams.SetSearchOption("temperature", temp);
                    var outputSequences = model.Generate(generatorParams);
                    Assert.NotNull(outputSequences);

                    var outputStrings = tokenizer.DecodeBatch(outputSequences);
                    Assert.NotNull(outputStrings);
                }
            }
        }

        [IgnoreOnModelAbsenceFact(DisplayName = "TestTopKTopPSearch")]
        public void TestTopKTopPSearch()
        {
            int topK = 100;
            float topP = 0.6f;
            float temp = 0.6f;
            ulong maxLength = 20;

            string modelPath = _phi2Path;
            using (var model = new Model(modelPath))
            {
                Assert.NotNull(model);
                using (var tokenizer = new Tokenizer(model))
                {
                    Assert.NotNull(tokenizer);

                    var strings = new string[] {
                        "This is a test.",
                        "Rats are awesome pets!",
                        "The quick brown fox jumps over the lazy dog."
                    };

                    var sequences = tokenizer.EncodeBatch(strings);
                    Assert.NotNull(sequences);
                    Assert.Equal((ulong)strings.Length, sequences.NumSequences);

                    using GeneratorParams generatorParams = new GeneratorParams(model);
                    Assert.NotNull(generatorParams);

                    generatorParams.SetInputSequences(sequences);
                    generatorParams.SetSearchOption("max_length", maxLength);
                    generatorParams.SetSearchOption("do_sample", true);
                    generatorParams.SetSearchOption("top_k", topK);
                    generatorParams.SetSearchOption("top_p", topP);
                    generatorParams.SetSearchOption("temperature", temp);
                    var outputSequences = model.Generate(generatorParams);
                    Assert.NotNull(outputSequences);

                    var outputStrings = tokenizer.DecodeBatch(outputSequences);
                    Assert.NotNull(outputStrings);
                }
            }
        }

        [IgnoreOnModelAbsenceFact(DisplayName = "TestTokenizerBatchEncodeDecode")]
        public void TestTokenizerBatchEncodeDecode()
        {
            string modelPath = _phi2Path;
            using (var model = new Model(modelPath))
            {
                Assert.NotNull(model);
                using (var tokenizer = new Tokenizer(model))
                {
                    Assert.NotNull(tokenizer);

                    var strings = new string[] {
                        "This is a test.",
                        "Rats are awesome pets!",
                        "The quick brown fox jumps over the lazy dog."
                    };

                    var sequences = tokenizer.EncodeBatch(strings);

                    Assert.NotNull(sequences);
                    Assert.Equal((ulong)strings.Length, sequences.NumSequences);

                    string[] decodedStrings = tokenizer.DecodeBatch(sequences);
                    Assert.NotNull(decodedStrings);
                    Assert.Equal(strings, decodedStrings);
                }
            }
        }

        [IgnoreOnModelAbsenceFact(DisplayName = "TestTokenizerBatchEncodeSingleDecode")]
        public void TestTokenizerBatchEncodeSingleDecode()
        {
            string modelPath = _phi2Path;
            using (var model = new Model(modelPath))
            {
                Assert.NotNull(model);
                using (var tokenizer = new Tokenizer(model))
                {
                    Assert.NotNull(tokenizer);

                    var strings = new string[] {
                        "This is a test.",
                        "Rats are awesome pets!",
                        "The quick brown fox jumps over the lazy dog."
                    };

                    var sequences = tokenizer.EncodeBatch(strings);

                    Assert.NotNull(sequences);
                    Assert.Equal((ulong)strings.Length, sequences.NumSequences);

                    for (ulong i = 0; i < sequences.NumSequences; i++)
                    {
                        var decodedString = tokenizer.Decode(sequences[i]);
                        Assert.Equal(strings[i], decodedString);
                    }
                }
            }
        }

        [IgnoreOnModelAbsenceFact(DisplayName = "TestTokenizerBatchEncodeStreamDecode")]
        public void TestTokenizerBatchEncodeStreamDecode()
        {
            string modelPath = _phi2Path;
            using (var model = new Model(modelPath))
            {
                Assert.NotNull(model);
                using (var tokenizer = new Tokenizer(model))
                {
                    Assert.NotNull(tokenizer);
                    var tokenizerStream = tokenizer.CreateStream();

                    var strings = new string[] {
                        "This is a test.",
                        "Rats are awesome pets!",
                        "The quick brown fox jumps over the lazy dog."
                    };

                    var sequences = tokenizer.EncodeBatch(strings);

                    Assert.NotNull(sequences);
                    Assert.Equal((ulong)strings.Length, sequences.NumSequences);

                    for (ulong i = 0; i < sequences.NumSequences; i++)
                    {
                        string decodedString = "";
                        for (int j = 0; j < sequences[i].Length; j++)
                        {
                            decodedString += tokenizerStream.Decode(sequences[i][j]);
                        }
                        Assert.Equal(strings[i], decodedString);
                    }
                }
            }
        }

        [IgnoreOnModelAbsenceFact(DisplayName = "TestTokenizerSingleEncodeDecode")]
        public void TestTokenizerSingleEncodeDecode()
        {
            string modelPath = _phi2Path;
            using (var model = new Model(modelPath))
            {
                Assert.NotNull(model);
                using (var tokenizer = new Tokenizer(model))
                {
                    Assert.NotNull(tokenizer);
                    var tokenizerStream = tokenizer.CreateStream();

                    var str = "She sells sea shells by the sea shore.";

                    var sequences = tokenizer.Encode(str);

                    Assert.NotNull(sequences);

                    string decodedString = tokenizer.Decode(sequences[0]);
                    Assert.Equal(str, decodedString);
                }
            }
        }

        [IgnoreOnModelAbsenceFact(DisplayName = "TestPhi2")]
        public void TestPhi2()
        {
            string modelPath = _phi2Path;
            using (var model = new Model(modelPath))
            {
                Assert.NotNull(model);
                using (var tokenizer = new Tokenizer(model))
                {
                    Assert.NotNull(tokenizer);

                    var strings = new string[] {
                        "This is a test.",
                        "Rats are awesome pets!",
                        "The quick brown fox jumps over the lazy dog."
                    };

                    var sequences = tokenizer.EncodeBatch(strings);
                    Assert.NotNull(sequences);
                    Assert.Equal((ulong)strings.Length, sequences.NumSequences);

                    using GeneratorParams generatorParams = new GeneratorParams(model);
                    Assert.NotNull(generatorParams);

                    generatorParams.SetSearchOption("max_length", 20);
                    generatorParams.SetInputSequences(sequences);

                    var outputSequences = model.Generate(generatorParams);
                    Assert.NotNull(outputSequences);

                    var outputStrings = tokenizer.DecodeBatch(outputSequences);
                    Assert.NotNull(outputStrings);
                }
            }
        }

        [Fact(DisplayName = "TestTensorAndAddExtraInput")]
        public void TestTensorAndAddExtraInput()
        {
            string modelPath = Path.Combine(Directory.GetCurrentDirectory(), "test_models", "hf-internal-testing", "tiny-random-gpt2-fp32");
            using var model = new Model(modelPath);
            Assert.NotNull(model);

            using var generatorParams = new GeneratorParams(model);
            Assert.NotNull(generatorParams);

            float[] data = { 0, 1, 2, 3, 4, 10, 11, 12, 13, 14, 20, 21, 22, 23, 24 };
            long[] shape = { 3, 5 };

            // Pin the array to get its pointer
            GCHandle handle = GCHandle.Alloc(data, GCHandleType.Pinned);
            try
            {
                IntPtr data_pointer = handle.AddrOfPinnedObject();

                using var tensor = new Tensor(data_pointer, shape, ElementType.float32);
                Assert.NotNull(tensor);

                Assert.Equal(shape, tensor.Shape());
                Assert.Equal(ElementType.float32, tensor.Type());

                generatorParams.SetModelInput("test_input", tensor);
            }
            finally
            {
                handle.Free();
            }
        }

        private class IgnoreOnAdaptersAbsentFact : FactAttribute
        {
            public IgnoreOnAdaptersAbsentFact()
            {
                string modelPath = Path.Combine(Directory.GetCurrentDirectory(), "test_models", "adapters");
                bool exists = System.IO.Directory.Exists(modelPath);
                if (!System.IO.Directory.Exists(modelPath))
                {
                    // Skip this test on some machines since the model cannot be downloaded on those machines at runtime.
                    Skip = "Skipping this test since the model does not exist.";
                }
            }
        }

        // This model is dependent on the presense of Phi2 model
        // get this model generated and copied to the output
        // by running test_onnxruntime_genai.py
        [IgnoreOnAdaptersAbsentFact(DisplayName = "TestAdapters")]
        public void TestAdapters()
        {
            string modelPath = Path.Combine(Directory.GetCurrentDirectory(), "test_models", "adapters");
            string adapterPath = Path.Combine(modelPath, "adapters.onnx_adapter");

            using var model = new Model(modelPath);
            Assert.NotNull(model);

            using var adapters = new Adapters(model);
            adapters.LoadAdapter(adapterPath, "adapters_a_and_b");

            var inputStrings = new string[]
            {
                "This is a test.",
                "Rats are awesome pets!",
                "The quick brown fox jumps over the lazy dog.",
            };

            using var tokenizer = new Tokenizer(model);
            using var sequences = tokenizer.EncodeBatch(inputStrings);

            Int64 outputSize = 0;
            Int64[] output_shape;
            float[] base_output;

            // Run base scenario
            {
                using var genParams = new GeneratorParams(model);
                genParams.SetSearchOption("max_length", 20);
                genParams.SetInputSequences(sequences);

                using var generator = new Generator(model, genParams);
                while(!generator.IsDone())
                {
                    generator.ComputeLogits();
                    generator.GenerateNextToken();
                }

                using var logits = generator.GetOutput("logits");
                Assert.Equal(ElementType.float32, logits.Type());
                output_shape = logits.Shape();
                outputSize = logits.NumElements();
                base_output = logits.GetData<float>().ToArray();
            }
            // Adapter scenario. The output must be affected
            {
                using var genParams = new GeneratorParams(model);
                genParams.SetSearchOption("max_length", 20);
                genParams.SetInputSequences(sequences);

                using var generator = new Generator(model, genParams);
                generator.SetActiveAdapter(adapters, "adapters_a_and_b");
                while (!generator.IsDone())
                {
                    generator.ComputeLogits();
                    generator.GenerateNextToken();
                }
                using var logits = generator.GetOutput("logits");
                Assert.Equal(ElementType.float32, logits.Type());
                Assert.Equal(outputSize, logits.NumElements());
                Assert.Equal(output_shape, logits.Shape());

                var adapter_output = logits.GetData<float>().ToArray();
                Assert.NotEqual(base_output, adapter_output);
            }
        }
    }
}
