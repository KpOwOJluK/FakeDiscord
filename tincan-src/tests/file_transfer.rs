//! End-to-end tests for the experimental P2P file plane.
//!
//! The test uses two offline iroh endpoints with a hand-fed address book, so no public
//! relay or DNS service is required. Bytes still travel through a real QUIC connection.

use std::path::PathBuf;
use std::time::{SystemTime, UNIX_EPOCH};

use anyhow::{Context, Result, ensure};
use tincan::net::endpoint::{bind_offline_with_lookup, to_peer_id};
use tincan::net::file::FileService;
use tincan::proto::{self, ChannelId};
use tokio::sync::mpsc;

/// Creates a unique temporary directory for one test run.
fn temp_directory() -> PathBuf {
    let nonce = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap()
        .as_nanos();

    std::env::temp_dir().join(format!("tincan-file-test-{}-{nonce}", std::process::id()))
}
/// A real binary stream must arrive intact and be finalized without leaving .part data.
#[tokio::test]
async fn file_flows_directly_between_two_peers() -> Result<()> {
    let (sender_endpoint, sender_lookup) = bind_offline_with_lookup().await?;
    let (receiver_endpoint, receiver_lookup) = bind_offline_with_lookup().await?;

    sender_lookup.add_endpoint_info(receiver_endpoint.addr());
    receiver_lookup.add_endpoint_info(sender_endpoint.addr());

    let root = temp_directory();
    let source_dir = root.join("source");
    let destination_dir = root.join("received");
    tokio::fs::create_dir_all(&source_dir).await?;

    let source_path = source_dir.join("example.bin");

    // Deliberately larger than one copy buffer so the streaming loop crosses chunks.
    let payload: Vec<u8> = (0..200_000usize)
        .map(|index| ((index * 37 + 11) % 251) as u8)
        .collect();

    tokio::fs::write(&source_path, &payload)
        .await
        .context("could not create the source test file")?;

    let (sender_events, _sender_rx) = mpsc::channel(32);
    let sender_files = FileService::new(sender_events);
    let sender_id = to_peer_id(sender_endpoint.id());
    let offer = sender_files
        .prepare_offer(source_path.clone(), sender_id, ChannelId(0), None)
        .await?;

    let accept_endpoint = sender_endpoint.clone();
    let accept_files = sender_files.clone();

    let accept_task = tokio::spawn(async move {
        let incoming = accept_endpoint
            .accept()
            .await
            .ok_or_else(|| anyhow::anyhow!("sender endpoint stopped accepting"))?;

        let mut accepting = incoming
            .accept()
            .context("could not begin accepting the test connection")?;

        let alpn = accepting
            .alpn()
            .await
            .context("could not read the test connection ALPN")?;

        ensure!(
            alpn == proto::FILE_ALPN,
            "expected FILE_ALPN, received {:?}",
            alpn
        );

        let connection = accepting
            .await
            .context("could not establish the test file connection")?;

        accept_files.accept(connection).await
    });

    let (receiver_events, _receiver_rx) = mpsc::channel(32);
    let receiver_files = FileService::new(receiver_events);
    let saved_path = receiver_files
        .download(
            receiver_endpoint.clone(),
            offer.clone(),
            ChannelId(0),
            Some(destination_dir.clone()),
        )
        .await?;

    accept_task.await.context("sender task panicked")??;

    let received = tokio::fs::read(&saved_path)
        .await
        .context("could not read the downloaded test file")?;

    assert_eq!(received, payload, "downloaded bytes must match exactly");
    assert_eq!(
        saved_path.file_name().and_then(|name| name.to_str()),
        Some("example.bin")
    );

    let mut directory = tokio::fs::read_dir(&destination_dir).await?;
    while let Some(entry) = directory.next_entry().await? {
        let name = entry.file_name().to_string_lossy().into_owned();
        assert!(
            !name.ends_with(".part"),
            "successful transfer must not leave a .part file: {name}"
        );
    }

    sender_endpoint.close().await;
    receiver_endpoint.close().await;
    let _ = tokio::fs::remove_dir_all(&root).await;
    Ok(())
}

/// A public file may only be fetched from the text channel where it was posted.
#[tokio::test]
async fn public_file_is_rejected_from_another_channel() -> Result<()> {
    let (sender_endpoint, _sender_lookup) = bind_offline_with_lookup().await?;
    let (receiver_endpoint, receiver_lookup) = bind_offline_with_lookup().await?;
    receiver_lookup.add_endpoint_info(sender_endpoint.addr());

    let root = temp_directory();
    tokio::fs::create_dir_all(&root).await?;
    let source_path = root.join("channel-bound.txt");
    tokio::fs::write(&source_path, b"channel scoped").await?;

    let (events, _rx) = mpsc::channel(32);
    let files = FileService::new(events);
    let offer = files
        .prepare_offer(
            source_path,
            to_peer_id(sender_endpoint.id()),
            ChannelId(0),
            None,
        )
        .await?;

    let accept_endpoint = sender_endpoint.clone();
    let accept_files = files.clone();
    let accept_task = tokio::spawn(async move {
        let incoming = accept_endpoint
            .accept()
            .await
            .ok_or_else(|| anyhow::anyhow!("sender stopped accepting"))?;
        let accepting = incoming.accept()?;
        let connection = accepting.await?;
        accept_files.accept(connection).await
    });

    let (receiver_events, _receiver_rx) = mpsc::channel(32);
    let receiver_files = FileService::new(receiver_events);
    let err = receiver_files
        .download(
            receiver_endpoint.clone(),
            offer,
            ChannelId(1),
            Some(root.join("received")),
        )
        .await
        .expect_err("another channel must not be allowed to fetch a public file");

    assert!(
        err.to_string().contains("open the channel"),
        "rejection should explain the channel rule: {err:#}"
    );
    accept_task.await??;

    sender_endpoint.close().await;
    receiver_endpoint.close().await;
    let _ = tokio::fs::remove_dir_all(root).await;
    Ok(())
}

/// A private file is bound to the recipient's peer identity, not to a channel.
#[tokio::test]
async fn private_file_only_reaches_recipient_and_works_from_any_channel() -> Result<()> {
    let (sender_endpoint, _sender_lookup) = bind_offline_with_lookup().await?;
    let (receiver_endpoint, receiver_lookup) = bind_offline_with_lookup().await?;
    let (intruder_endpoint, intruder_lookup) = bind_offline_with_lookup().await?;
    receiver_lookup.add_endpoint_info(sender_endpoint.addr());
    intruder_lookup.add_endpoint_info(sender_endpoint.addr());

    let root = temp_directory();
    tokio::fs::create_dir_all(&root).await?;
    let source_path = root.join("private.bin");
    let payload = b"for the intended recipient only".to_vec();
    tokio::fs::write(&source_path, &payload).await?;

    let (events, _rx) = mpsc::channel(32);
    let files = FileService::new(events);
    let receiver_id = to_peer_id(receiver_endpoint.id());
    let offer = files
        .prepare_offer(
            source_path,
            to_peer_id(sender_endpoint.id()),
            ChannelId(0),
            Some(receiver_id),
        )
        .await?;

    let accept_endpoint = sender_endpoint.clone();
    let accept_files = files.clone();
    let accept_task = tokio::spawn(async move {
        for _ in 0..2 {
            let incoming = accept_endpoint
                .accept()
                .await
                .ok_or_else(|| anyhow::anyhow!("sender stopped accepting"))?;
            let accepting = incoming.accept()?;
            let connection = accepting.await?;
            accept_files.accept(connection).await?;
        }
        Ok::<_, anyhow::Error>(())
    });

    let (intruder_events, _intruder_rx) = mpsc::channel(32);
    let intruder_files = FileService::new(intruder_events);
    let err = intruder_files
        .download(
            intruder_endpoint.clone(),
            offer.clone(),
            ChannelId(0),
            Some(root.join("intruder")),
        )
        .await
        .expect_err("a different peer must not receive a private file");
    assert!(
        err.to_string().contains("another participant"),
        "private rejection should identify the recipient rule: {err:#}"
    );

    let (receiver_events, _receiver_rx) = mpsc::channel(32);
    let receiver_files = FileService::new(receiver_events);
    let saved = receiver_files
        .download(
            receiver_endpoint.clone(),
            offer,
            ChannelId(7),
            Some(root.join("recipient")),
        )
        .await?;
    assert_eq!(tokio::fs::read(saved).await?, payload);

    accept_task.await??;
    sender_endpoint.close().await;
    receiver_endpoint.close().await;
    intruder_endpoint.close().await;
    let _ = tokio::fs::remove_dir_all(root).await;
    Ok(())
}

/// Image offers carry a tiny decoded RGB preview, not the original image bytes.
#[tokio::test]
async fn image_offer_contains_small_preview() -> Result<()> {
    let (sender_endpoint, _lookup) = bind_offline_with_lookup().await?;
    let root = temp_directory();
    tokio::fs::create_dir_all(&root).await?;
    let source_path = root.join("preview.png");

    let image = image::RgbImage::from_pixel(40, 20, image::Rgb([20, 120, 220]));
    image.save(&source_path)?;

    let (events, _rx) = mpsc::channel(32);
    let files = FileService::new(events);
    let offer = files
        .prepare_offer(
            source_path,
            to_peer_id(sender_endpoint.id()),
            ChannelId(0),
            None,
        )
        .await?;

    match offer.preview {
        tincan::proto::FilePreview::Image {
            width,
            height,
            thumb_width,
            thumb_height,
            rgb,
        } => {
            assert_eq!((width, height), (40, 20));
            assert!(thumb_width <= 24);
            assert!(thumb_height <= 12);
            assert_eq!(rgb.len(), thumb_width as usize * thumb_height as usize * 3);
        }
        other => panic!("expected image preview, got {other:?}"),
    }

    sender_endpoint.close().await;
    let _ = tokio::fs::remove_dir_all(root).await;
    Ok(())
}
