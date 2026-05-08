using System.Text.RegularExpressions;
using System.Globalization;
using Microsoft.Data.Sqlite;
using Microsoft.Extensions.Configuration;

IConfiguration configuration = new ConfigurationBuilder()
    .SetBasePath(AppContext.BaseDirectory)
    .AddJsonFile("appsettings.json", optional: false, reloadOnChange: false)
    .Build();

string cangjieFile = ResolvePath(GetRequiredConfigValue(configuration, "Input:CangjieFile"));
string associatedWordsFolder = ResolvePath(GetRequiredConfigValue(configuration, "Input:AssociatedWordsFolder"));
string databaseFile = ResolvePath(GetRequiredConfigValue(configuration, "Output:DatabaseFile"));
AssociatedWordsBuildOptions associatedWordsBuildOptions = LoadAssociatedWordsBuildOptions(configuration);

Console.WriteLine($"Cangjie file: {cangjieFile}");
Console.WriteLine($"Associated words folder: {associatedWordsFolder}");
Console.WriteLine($"Database file: {databaseFile}");
Console.WriteLine(
    "Associated words build options: " +
    $"N={associatedWordsBuildOptions.SlidingWindowSize}, " +
    $"leading_max_length={associatedWordsBuildOptions.LeadingMaxLength}, " +
    $"associated_max_length={associatedWordsBuildOptions.AssociatedMaxLength}, " +
    $"K={associatedWordsBuildOptions.MaxEntriesPerLeadingText}");

ImportCangjieMappings(cangjieFile, databaseFile);
ImportCharacterUsage(associatedWordsFolder, databaseFile);
RebuildAssociatedWords(associatedWordsFolder, databaseFile, associatedWordsBuildOptions);

Console.WriteLine("Completed");

static void ImportCangjieMappings(string cangjieFile, string databaseFile)
{
    if (!File.Exists(cangjieFile))
        throw new FileNotFoundException("Cangjie source file was not found.", cangjieFile);

    if (!File.Exists(databaseFile))
        throw new FileNotFoundException("SQLite database file was not found.", databaseFile);

    using var connection = new SqliteConnection($"Data Source={databaseFile}");
    connection.Open();

    using var transaction = connection.BeginTransaction();

    ExecuteNonQuery(connection, transaction, "PRAGMA foreign_keys = ON;");
    ExecuteNonQuery(connection, transaction, "DELETE FROM ime_cangjie_mapping;");

    using var insertCharacter = connection.CreateCommand();
    insertCharacter.Transaction = transaction;
    insertCharacter.CommandText =
        """
        INSERT OR IGNORE INTO ime_character (character, usage_count)
        VALUES ($character, 0);
        """;
    SqliteParameter characterForCharacterTable = insertCharacter.Parameters.Add("$character", SqliteType.Text);

    using var insertMapping = connection.CreateCommand();
    insertMapping.Transaction = transaction;
    insertMapping.CommandText =
        """
        INSERT OR IGNORE INTO ime_cangjie_mapping (character, code, line_number)
        VALUES ($character, $code, $line_number);
        """;
    SqliteParameter characterForMappingTable = insertMapping.Parameters.Add("$character", SqliteType.Text);
    SqliteParameter codeForMappingTable = insertMapping.Parameters.Add("$code", SqliteType.Text);
    SqliteParameter lineNumberForMappingTable = insertMapping.Parameters.Add("$line_number", SqliteType.Integer);

    int insertedMappings = 0;
    int skippedLines = 0;
    int lineNumber = 0;

    foreach (string line in File.ReadLines(cangjieFile))
    {
        lineNumber++;

        string trimmedLine = line.Trim();
        if (trimmedLine.Length == 0)
        {
            skippedLines++;
            continue;
        }

        string[] fields = Regex.Split(trimmedLine, @"\s+");
        if (fields.Length < 2)
        {
            skippedLines++;
            continue;
        }

        string character = fields[0];
        string code = fields[1];

        characterForCharacterTable.Value = character;
        insertCharacter.ExecuteNonQuery();

        characterForMappingTable.Value = character;
        codeForMappingTable.Value = code;
        lineNumberForMappingTable.Value = lineNumber;
        insertedMappings += insertMapping.ExecuteNonQuery();
    }

    transaction.Commit();

    Console.WriteLine($"Inserted Cangjie mappings: {insertedMappings}");
    Console.WriteLine($"Skipped Cangjie lines: {skippedLines}");
}

static void ImportCharacterUsage(string associatedWordsFolder, string databaseFile)
{
    if (!Directory.Exists(associatedWordsFolder))
        throw new DirectoryNotFoundException($"Associated words folder was not found: {associatedWordsFolder}");

    if (!File.Exists(databaseFile))
        throw new FileNotFoundException("SQLite database file was not found.", databaseFile);

    using var connection = new SqliteConnection($"Data Source={databaseFile}");
    connection.Open();

    using var transaction = connection.BeginTransaction();

    ExecuteNonQuery(connection, transaction, "PRAGMA foreign_keys = ON;");
    ExecuteNonQuery(connection, transaction, "UPDATE ime_character SET usage_count = 0;");

    using var incrementCharacter = connection.CreateCommand();
    incrementCharacter.Transaction = transaction;
    incrementCharacter.CommandText =
        """
        UPDATE ime_character
        SET usage_count = usage_count + 1
        WHERE character = $character;
        """;
    SqliteParameter characterForIncrement = incrementCharacter.Parameters.Add("$character", SqliteType.Text);

    int fileCount = 0;
    long characterCount = 0;
    long skippedUnknownCharacters = 0;

    foreach (string file in Directory.EnumerateFiles(associatedWordsFolder, "*", SearchOption.AllDirectories))
    {
        fileCount++;
        string text = File.ReadAllText(file);
        TextElementEnumerator textElements = StringInfo.GetTextElementEnumerator(text);

        while (textElements.MoveNext())
        {
            string character = textElements.GetTextElement();
            if (IsSkippableCharacter(character))
                continue;

            characterForIncrement.Value = character;
            if (incrementCharacter.ExecuteNonQuery() > 0)
                characterCount++;
            else
                skippedUnknownCharacters++;
        }
    }

    transaction.Commit();

    Console.WriteLine($"Scanned associated word files: {fileCount}");
    Console.WriteLine($"Counted associated word characters: {characterCount}");
    Console.WriteLine($"Skipped characters not found in database: {skippedUnknownCharacters}");
}

static void RebuildAssociatedWords(
    string associatedWordsFolder,
    string databaseFile,
    AssociatedWordsBuildOptions options)
{
    if (!Directory.Exists(associatedWordsFolder))
        throw new DirectoryNotFoundException($"Associated words folder was not found: {associatedWordsFolder}");

    if (!File.Exists(databaseFile))
        throw new FileNotFoundException("SQLite database file was not found.", databaseFile);

    using var connection = new SqliteConnection($"Data Source={databaseFile}");
    connection.Open();

    using var transaction = connection.BeginTransaction();

    ExecuteNonQuery(connection, transaction, "PRAGMA foreign_keys = ON;");
    ExecuteNonQuery(connection, transaction, "DELETE FROM ime_metadata WHERE key LIKE 'associated_%';");
    ExecuteNonQuery(connection, transaction, "DELETE FROM ime_character_associated_word;");

    using var upsertAssociatedWord = connection.CreateCommand();
    upsertAssociatedWord.Transaction = transaction;
    upsertAssociatedWord.CommandText =
        """
        INSERT INTO ime_character_associated_word (leading_text, associated_text, usage_count)
        VALUES ($leading_text, $associated_text, $usage_count)
        ON CONFLICT (leading_text, associated_text)
        DO UPDATE SET usage_count = ime_character_associated_word.usage_count + excluded.usage_count;
        """;
    SqliteParameter leadingTextParameter = upsertAssociatedWord.Parameters.Add("$leading_text", SqliteType.Text);
    SqliteParameter associatedTextParameter = upsertAssociatedWord.Parameters.Add("$associated_text", SqliteType.Text);
    SqliteParameter usageCountParameter = upsertAssociatedWord.Parameters.Add("$usage_count", SqliteType.Integer);

    int fileCount = 0;
    long pairOccurrences = 0;
    long weightedPairCount = 0;

    foreach (string file in EnumerateCorpusFiles(associatedWordsFolder))
    {
        fileCount++;
        int fileWeight = GetCorpusFileWeight(file);

        foreach (string line in File.ReadLines(file))
        {
            List<string> textElements = GetTextElements(line);

            for (int i = 0; i < textElements.Count; i++)
            {
                for (int leadingLength = 1; leadingLength <= options.LeadingMaxLength; leadingLength++)
                {
                    if (i + leadingLength >= textElements.Count)
                        break;

                    string leadingText = string.Concat(textElements.Skip(i).Take(leadingLength));
                    if (IsSkippableToken(leadingText))
                        continue;

                    for (int associatedLength = 1; associatedLength <= options.AssociatedMaxLength; associatedLength++)
                    {
                        if (i + leadingLength + associatedLength > textElements.Count)
                            break;

                        string associatedText = string.Concat(textElements.Skip(i + leadingLength).Take(associatedLength));
                        if (IsSkippableToken(associatedText))
                            continue;

                        leadingTextParameter.Value = leadingText;
                        associatedTextParameter.Value = associatedText;
                        usageCountParameter.Value = fileWeight;
                        upsertAssociatedWord.ExecuteNonQuery();

                        pairOccurrences++;
                        weightedPairCount += fileWeight;
                    }
                }
            }
        }
    }

    PruneAssociatedWords(connection, transaction, options.MaxEntriesPerLeadingText);
    WriteAssociatedWordsMetadata(connection, transaction, options);

    transaction.Commit();

    Console.WriteLine($"Rebuilt associated word files: {fileCount}");
    Console.WriteLine($"Associated word pair occurrences: {pairOccurrences}");
    Console.WriteLine($"Weighted associated word count: {weightedPairCount}");
}

static void WriteAssociatedWordsMetadata(
    SqliteConnection connection,
    SqliteTransaction transaction,
    AssociatedWordsBuildOptions options)
{
    using var command = connection.CreateCommand();
    command.Transaction = transaction;
    command.CommandText =
        """
        INSERT INTO ime_metadata (key, value)
        VALUES
            ('associated_leading_max_length', $leading_max_length),
            ('associated_max_length', $associated_max_length),
            ('associated_max_entries_per_leading_text', $max_entries_per_leading_text)
        ON CONFLICT (key)
        DO UPDATE SET value = excluded.value;
        """;
    command.Parameters.AddWithValue("$leading_max_length", options.LeadingMaxLength.ToString(CultureInfo.InvariantCulture));
    command.Parameters.AddWithValue("$associated_max_length", options.AssociatedMaxLength.ToString(CultureInfo.InvariantCulture));
    command.Parameters.AddWithValue("$max_entries_per_leading_text", options.MaxEntriesPerLeadingText.ToString(CultureInfo.InvariantCulture));
    command.ExecuteNonQuery();
}

static void PruneAssociatedWords(SqliteConnection connection, SqliteTransaction transaction, int maxEntriesPerLeadingText)
{
    using var prune = connection.CreateCommand();
    prune.Transaction = transaction;
    prune.CommandText =
        """
        CREATE TEMP TABLE tmp_assoc AS
        SELECT leading_text, associated_text, usage_count
        FROM (
            SELECT
                id,
                leading_text,
                associated_text,
                usage_count,
                ROW_NUMBER() OVER (
                    PARTITION BY leading_text
                    ORDER BY usage_count DESC,
                             length(associated_text) DESC,
                             associated_text ASC,
                             id ASC
                ) AS rn
            FROM ime_character_associated_word
        )
        WHERE rn <= $max_entries;

        DELETE FROM ime_character_associated_word;

        INSERT INTO ime_character_associated_word (leading_text, associated_text, usage_count)
        SELECT leading_text, associated_text, usage_count
        FROM tmp_assoc
        ORDER BY leading_text ASC,
                 usage_count DESC,
                 length(associated_text) DESC,
                 associated_text ASC;

        DROP TABLE tmp_assoc;
        """;
    prune.Parameters.AddWithValue("$max_entries", maxEntriesPerLeadingText);
    prune.ExecuteNonQuery();
}

static void ExecuteNonQuery(SqliteConnection connection, SqliteTransaction transaction, string commandText)
{
    using var command = connection.CreateCommand();
    command.Transaction = transaction;
    command.CommandText = commandText;
    command.ExecuteNonQuery();
}

static bool IsSkippableCharacter(string character)
{
    foreach (char ch in character)
    {
        if (char.IsWhiteSpace(ch) || char.IsControl(ch))
            return true;
    }

    return false;
}

static bool IsSkippableToken(string token)
{
    foreach (char ch in token)
    {
        if (char.IsWhiteSpace(ch) || char.IsControl(ch) || IsAsciiLetterOrDigit(ch))
            return true;
    }

    return false;
}

static bool IsAsciiLetterOrDigit(char ch)
{
    return (ch >= '0' && ch <= '9') ||
           (ch >= 'A' && ch <= 'Z') ||
           (ch >= 'a' && ch <= 'z');
}

static IEnumerable<string> EnumerateCorpusFiles(string associatedWordsFolder)
{
    return Directory
        .EnumerateFiles(associatedWordsFolder, "*", SearchOption.AllDirectories)
        .OrderBy(file => file, StringComparer.Ordinal);
}

static int GetCorpusFileWeight(string file)
{
    Match match = Regex.Match(Path.GetFileName(file), @"^([1-9]\d*)_");
    if (!match.Success)
        return 1;

    if (!int.TryParse(match.Groups[1].Value, NumberStyles.None, CultureInfo.InvariantCulture, out int weight))
        return 1;

    return weight;
}

static List<string> GetTextElements(string text)
{
    List<string> textElements = [];
    TextElementEnumerator textElementEnumerator = StringInfo.GetTextElementEnumerator(text);

    while (textElementEnumerator.MoveNext())
        textElements.Add(textElementEnumerator.GetTextElement());

    return textElements;
}

static string GetRequiredConfigValue(IConfiguration configuration, string key)
{
    string? value = configuration[key];
    if (string.IsNullOrWhiteSpace(value))
        throw new InvalidOperationException($"Missing required configuration value: {key}");

    return value;
}

static AssociatedWordsBuildOptions LoadAssociatedWordsBuildOptions(IConfiguration configuration)
{
    int slidingWindowSize = GetOptionalPositiveInt(configuration, "Associated:N", 4);
    int leadingMaxLength = GetOptionalPositiveInt(configuration, "Associated:leading_max_length", 3);
    int associatedMaxLength = GetOptionalPositiveInt(configuration, "Associated:associated_max_length", 3);
    int maxEntriesPerLeadingText = GetOptionalPositiveInt(configuration, "Associated:K", 50);

    if (leadingMaxLength > 8)
        throw new InvalidOperationException("Associated:leading_max_length must be less than or equal to 8.");

    if (associatedMaxLength > 8)
        throw new InvalidOperationException("Associated:associated_max_length must be less than or equal to 8.");

    return new AssociatedWordsBuildOptions(
        slidingWindowSize,
        leadingMaxLength,
        associatedMaxLength,
        maxEntriesPerLeadingText);
}

static int GetOptionalPositiveInt(IConfiguration configuration, string key, int defaultValue)
{
    string? value = configuration[key];
    if (string.IsNullOrWhiteSpace(value))
        return defaultValue;

    if (!int.TryParse(value, NumberStyles.None, CultureInfo.InvariantCulture, out int result) || result < 1)
        throw new InvalidOperationException($"{key} must be a positive integer.");

    return result;
}

static string ResolvePath(string path)
{
    if (Path.IsPathRooted(path))
        return Path.GetFullPath(path);

    return Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, path));
}

internal sealed record AssociatedWordsBuildOptions(
    int SlidingWindowSize,
    int LeadingMaxLength,
    int AssociatedMaxLength,
    int MaxEntriesPerLeadingText);
